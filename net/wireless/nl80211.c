/*
 * This is the new netlink-based wireless configuration interface.
 *
 * Copyright 2006-2009	Johannes Berg <johannes@sipsolutions.net>
 */

#include <linux/if.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/if_ether.h>
#include <linux/ieee80211.h>
#include <linux/nl80211.h>
#include <linux/rtnetlink.h>
#include <linux/netlink.h>
#include <linux/etherdevice.h>
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
				      .len = 20-1 },
	[NL80211_ATTR_WIPHY_TXQ_PARAMS] = { .type = NLA_NESTED },
	[NL80211_ATTR_WIPHY_FREQ] = { .type = NLA_U32 },
	[NL80211_ATTR_WIPHY_CHANNEL_TYPE] = { .type = NLA_U32 },
	[NL80211_ATTR_WIPHY_RETRY_SHORT] = { .type = NLA_U8 },
	[NL80211_ATTR_WIPHY_RETRY_LONG] = { .type = NLA_U8 },
	[NL80211_ATTR_WIPHY_FRAG_THRESHOLD] = { .type = NLA_U32 },
	[NL80211_ATTR_WIPHY_RTS_THRESHOLD] = { .type = NLA_U32 },

	[NL80211_ATTR_IFTYPE] = { .type = NLA_U32 },
	[NL80211_ATTR_IFINDEX] = { .type = NLA_U32 },
	[NL80211_ATTR_IFNAME] = { .type = NLA_NUL_STRING, .len = IFNAMSIZ-1 },

	[NL80211_ATTR_MAC] = { .type = NLA_BINARY, .len = ETH_ALEN },

	[NL80211_ATTR_KEY_DATA] = { .type = NLA_BINARY,
				    .len = WLAN_MAX_KEY_LEN },
	[NL80211_ATTR_KEY_IDX] = { .type = NLA_U8 },
	[NL80211_ATTR_KEY_CIPHER] = { .type = NLA_U32 },
	[NL80211_ATTR_KEY_DEFAULT] = { .type = NLA_FLAG },
	[NL80211_ATTR_KEY_SEQ] = { .type = NLA_BINARY, .len = 8 },

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

	[NL80211_ATTR_MGMT_SUBTYPE] = { .type = NLA_U8 },
	[NL80211_ATTR_IE] = { .type = NLA_BINARY,
			      .len = IEEE80211_MAX_DATA_LEN },
	[NL80211_ATTR_SCAN_FREQUENCIES] = { .type = NLA_NESTED },
	[NL80211_ATTR_SCAN_SSIDS] = { .type = NLA_NESTED },

	[NL80211_ATTR_SSID] = { .type = NLA_BINARY,
				.len = IEEE80211_MAX_SSID_LEN },
	[NL80211_ATTR_AUTH_TYPE] = { .type = NLA_U32 },
	[NL80211_ATTR_REASON_CODE] = { .type = NLA_U16 },
	[NL80211_ATTR_FREQ_FIXED] = { .type = NLA_FLAG },
	[NL80211_ATTR_TIMED_OUT] = { .type = NLA_FLAG },
	[NL80211_ATTR_USE_MFP] = { .type = NLA_U32 },
	[NL80211_ATTR_STA_FLAGS2] = {
		.len = sizeof(struct nl80211_sta_flag_update),
	},
	[NL80211_ATTR_CONTROL_PORT] = { .type = NLA_FLAG },
};

/* IE validation */
static bool is_valid_ie_attr(const struct nlattr *attr)
{
	const u8 *pos;
	int len;

	if (!attr)
		return true;

	pos = nla_data(attr);
	len = nla_len(attr);

	while (len) {
		u8 elemlen;

		if (len < 2)
			return false;
		len -= 2;

		elemlen = pos[1];
		if (elemlen > len)
			return false;

		len -= elemlen;
		pos += 2 + elemlen;
	}

	return true;
}

/* message building helper */
static inline void *nl80211hdr_put(struct sk_buff *skb, u32 pid, u32 seq,
				   int flags, u8 cmd)
{
	/* since there is no private header just add the generic one */
	return genlmsg_put(skb, pid, seq, &nl80211_fam, flags, cmd);
}

static int nl80211_msg_put_channel(struct sk_buff *msg,
				   struct ieee80211_channel *chan)
{
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

	return 0;

 nla_put_failure:
	return -ENOBUFS;
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
	struct nlattr *nl_cmds;
	enum ieee80211_band band;
	struct ieee80211_channel *chan;
	struct ieee80211_rate *rate;
	int i;
	u16 ifmodes = dev->wiphy.interface_modes;

	hdr = nl80211hdr_put(msg, pid, seq, flags, NL80211_CMD_NEW_WIPHY);
	if (!hdr)
		return -1;

	NLA_PUT_U32(msg, NL80211_ATTR_WIPHY, dev->wiphy_idx);
	NLA_PUT_STRING(msg, NL80211_ATTR_WIPHY_NAME, wiphy_name(&dev->wiphy));

	NLA_PUT_U8(msg, NL80211_ATTR_WIPHY_RETRY_SHORT,
		   dev->wiphy.retry_short);
	NLA_PUT_U8(msg, NL80211_ATTR_WIPHY_RETRY_LONG,
		   dev->wiphy.retry_long);
	NLA_PUT_U32(msg, NL80211_ATTR_WIPHY_FRAG_THRESHOLD,
		    dev->wiphy.frag_threshold);
	NLA_PUT_U32(msg, NL80211_ATTR_WIPHY_RTS_THRESHOLD,
		    dev->wiphy.rts_threshold);

	NLA_PUT_U8(msg, NL80211_ATTR_MAX_NUM_SCAN_SSIDS,
		   dev->wiphy.max_scan_ssids);
	NLA_PUT_U16(msg, NL80211_ATTR_MAX_SCAN_IE_LEN,
		    dev->wiphy.max_scan_ie_len);

	NLA_PUT(msg, NL80211_ATTR_CIPHER_SUITES,
		sizeof(u32) * dev->wiphy.n_cipher_suites,
		dev->wiphy.cipher_suites);

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

			if (nl80211_msg_put_channel(msg, chan))
				goto nla_put_failure;

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

	nl_cmds = nla_nest_start(msg, NL80211_ATTR_SUPPORTED_COMMANDS);
	if (!nl_cmds)
		goto nla_put_failure;

	i = 0;
#define CMD(op, n)						\
	 do {							\
		if (dev->ops->op) {				\
			i++;					\
			NLA_PUT_U32(msg, i, NL80211_CMD_ ## n);	\
		}						\
	} while (0)

	CMD(add_virtual_intf, NEW_INTERFACE);
	CMD(change_virtual_intf, SET_INTERFACE);
	CMD(add_key, NEW_KEY);
	CMD(add_beacon, NEW_BEACON);
	CMD(add_station, NEW_STATION);
	CMD(add_mpath, NEW_MPATH);
	CMD(set_mesh_params, SET_MESH_PARAMS);
	CMD(change_bss, SET_BSS);
	CMD(auth, AUTHENTICATE);
	CMD(assoc, ASSOCIATE);
	CMD(deauth, DEAUTHENTICATE);
	CMD(disassoc, DISASSOCIATE);
	CMD(join_ibss, JOIN_IBSS);

#undef CMD
	nla_nest_end(msg, nl_cmds);

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

	mutex_lock(&cfg80211_mutex);
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
	mutex_unlock(&cfg80211_mutex);

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

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
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
	u32 changed;
	u8 retry_short = 0, retry_long = 0;
	u32 frag_threshold = 0, rts_threshold = 0;

	rtnl_lock();

	mutex_lock(&cfg80211_mutex);

	rdev = __cfg80211_drv_from_info(info);
	if (IS_ERR(rdev)) {
		mutex_unlock(&cfg80211_mutex);
		result = PTR_ERR(rdev);
		goto unlock;
	}

	mutex_lock(&rdev->mtx);

	if (info->attrs[NL80211_ATTR_WIPHY_NAME])
		result = cfg80211_dev_rename(
			rdev, nla_data(info->attrs[NL80211_ATTR_WIPHY_NAME]));

	mutex_unlock(&cfg80211_mutex);

	if (result)
		goto bad_res;

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
		u32 freq;

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

		if (channel_type == NL80211_CHAN_HT40MINUS &&
		    (chan->flags & IEEE80211_CHAN_NO_HT40MINUS))
			goto bad_res;
		else if (channel_type == NL80211_CHAN_HT40PLUS &&
			 (chan->flags & IEEE80211_CHAN_NO_HT40PLUS))
			goto bad_res;

		/*
		 * At this point we know if that if HT40 was requested
		 * we are allowed to use it and the extension channel
		 * exists.
		 */

		ht_cap = &rdev->wiphy.bands[chan->band]->ht_cap;

		/* no HT capabilities or intolerant */
		if (channel_type != NL80211_CHAN_NO_HT) {
			if (!ht_cap->ht_supported)
				goto bad_res;
			if (!(ht_cap->cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40) ||
			    (ht_cap->cap & IEEE80211_HT_CAP_40MHZ_INTOLERANT))
				goto bad_res;
		}

		result = rdev->ops->set_channel(&rdev->wiphy, chan,
						channel_type);
		if (result)
			goto bad_res;
	}

	changed = 0;

	if (info->attrs[NL80211_ATTR_WIPHY_RETRY_SHORT]) {
		retry_short = nla_get_u8(
			info->attrs[NL80211_ATTR_WIPHY_RETRY_SHORT]);
		if (retry_short == 0) {
			result = -EINVAL;
			goto bad_res;
		}
		changed |= WIPHY_PARAM_RETRY_SHORT;
	}

	if (info->attrs[NL80211_ATTR_WIPHY_RETRY_LONG]) {
		retry_long = nla_get_u8(
			info->attrs[NL80211_ATTR_WIPHY_RETRY_LONG]);
		if (retry_long == 0) {
			result = -EINVAL;
			goto bad_res;
		}
		changed |= WIPHY_PARAM_RETRY_LONG;
	}

	if (info->attrs[NL80211_ATTR_WIPHY_FRAG_THRESHOLD]) {
		frag_threshold = nla_get_u32(
			info->attrs[NL80211_ATTR_WIPHY_FRAG_THRESHOLD]);
		if (frag_threshold < 256) {
			result = -EINVAL;
			goto bad_res;
		}
		if (frag_threshold != (u32) -1) {
			/*
			 * Fragments (apart from the last one) are required to
			 * have even length. Make the fragmentation code
			 * simpler by stripping LSB should someone try to use
			 * odd threshold value.
			 */
			frag_threshold &= ~0x1;
		}
		changed |= WIPHY_PARAM_FRAG_THRESHOLD;
	}

	if (info->attrs[NL80211_ATTR_WIPHY_RTS_THRESHOLD]) {
		rts_threshold = nla_get_u32(
			info->attrs[NL80211_ATTR_WIPHY_RTS_THRESHOLD]);
		changed |= WIPHY_PARAM_RTS_THRESHOLD;
	}

	if (changed) {
		u8 old_retry_short, old_retry_long;
		u32 old_frag_threshold, old_rts_threshold;

		if (!rdev->ops->set_wiphy_params) {
			result = -EOPNOTSUPP;
			goto bad_res;
		}

		old_retry_short = rdev->wiphy.retry_short;
		old_retry_long = rdev->wiphy.retry_long;
		old_frag_threshold = rdev->wiphy.frag_threshold;
		old_rts_threshold = rdev->wiphy.rts_threshold;

		if (changed & WIPHY_PARAM_RETRY_SHORT)
			rdev->wiphy.retry_short = retry_short;
		if (changed & WIPHY_PARAM_RETRY_LONG)
			rdev->wiphy.retry_long = retry_long;
		if (changed & WIPHY_PARAM_FRAG_THRESHOLD)
			rdev->wiphy.frag_threshold = frag_threshold;
		if (changed & WIPHY_PARAM_RTS_THRESHOLD)
			rdev->wiphy.rts_threshold = rts_threshold;

		result = rdev->ops->set_wiphy_params(&rdev->wiphy, changed);
		if (result) {
			rdev->wiphy.retry_short = old_retry_short;
			rdev->wiphy.retry_long = old_retry_long;
			rdev->wiphy.frag_threshold = old_frag_threshold;
			rdev->wiphy.rts_threshold = old_rts_threshold;
		}
	}

 bad_res:
	mutex_unlock(&rdev->mtx);
 unlock:
	rtnl_unlock();
	return result;
}


static int nl80211_send_iface(struct sk_buff *msg, u32 pid, u32 seq, int flags,
			      struct cfg80211_registered_device *rdev,
			      struct net_device *dev)
{
	void *hdr;

	hdr = nl80211hdr_put(msg, pid, seq, flags, NL80211_CMD_NEW_INTERFACE);
	if (!hdr)
		return -1;

	NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, dev->ifindex);
	NLA_PUT_U32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx);
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

	mutex_lock(&cfg80211_mutex);
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
					       dev, wdev->netdev) < 0) {
				mutex_unlock(&dev->devlist_mtx);
				goto out;
			}
			if_idx++;
		}
		mutex_unlock(&dev->devlist_mtx);

		wp_idx++;
	}
 out:
	mutex_unlock(&cfg80211_mutex);

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

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		goto out_err;

	if (nl80211_send_iface(msg, info->snd_pid, info->snd_seq, 0,
			       dev, netdev) < 0)
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
	enum nl80211_iftype otype, ntype;
	struct net_device *dev;
	u32 _flags, *flags = NULL;
	bool change = false;

	memset(&params, 0, sizeof(params));

	rtnl_lock();

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		goto unlock_rtnl;

	ifindex = dev->ifindex;
	otype = ntype = dev->ieee80211_ptr->iftype;
	dev_put(dev);

	if (info->attrs[NL80211_ATTR_IFTYPE]) {
		ntype = nla_get_u32(info->attrs[NL80211_ATTR_IFTYPE]);
		if (otype != ntype)
			change = true;
		if (ntype > NL80211_IFTYPE_MAX) {
			err = -EINVAL;
			goto unlock;
		}
	}

	if (!drv->ops->change_virtual_intf ||
	    !(drv->wiphy.interface_modes & (1 << ntype))) {
		err = -EOPNOTSUPP;
		goto unlock;
	}

	if (info->attrs[NL80211_ATTR_MESH_ID]) {
		if (ntype != NL80211_IFTYPE_MESH_POINT) {
			err = -EINVAL;
			goto unlock;
		}
		params.mesh_id = nla_data(info->attrs[NL80211_ATTR_MESH_ID]);
		params.mesh_id_len = nla_len(info->attrs[NL80211_ATTR_MESH_ID]);
		change = true;
	}

	if (info->attrs[NL80211_ATTR_MNTR_FLAGS]) {
		if (ntype != NL80211_IFTYPE_MONITOR) {
			err = -EINVAL;
			goto unlock;
		}
		err = parse_monitor_flags(info->attrs[NL80211_ATTR_MNTR_FLAGS],
					  &_flags);
		if (err)
			goto unlock;

		flags = &_flags;
		change = true;
	}

	if (change)
		err = drv->ops->change_virtual_intf(&drv->wiphy, ifindex,
						    ntype, flags, &params);
	else
		err = 0;

	dev = __dev_get_by_index(&init_net, ifindex);
	WARN_ON(!dev || (!err && dev->ieee80211_ptr->iftype != ntype));

	if (dev && !err && (ntype != otype)) {
		if (otype == NL80211_IFTYPE_ADHOC)
			cfg80211_clear_ibss(dev, false);
	}

 unlock:
	cfg80211_put_dev(drv);
 unlock_rtnl:
	rtnl_unlock();
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

	rtnl_lock();

	drv = cfg80211_get_dev_from_info(info);
	if (IS_ERR(drv)) {
		err = PTR_ERR(drv);
		goto unlock_rtnl;
	}

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

	err = parse_monitor_flags(type == NL80211_IFTYPE_MONITOR ?
				  info->attrs[NL80211_ATTR_MNTR_FLAGS] : NULL,
				  &flags);
	err = drv->ops->add_virtual_intf(&drv->wiphy,
		nla_data(info->attrs[NL80211_ATTR_IFNAME]),
		type, err ? NULL : &flags, &params);

 unlock:
	cfg80211_put_dev(drv);
 unlock_rtnl:
	rtnl_unlock();
	return err;
}

static int nl80211_del_interface(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *drv;
	int ifindex, err;
	struct net_device *dev;

	rtnl_lock();

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		goto unlock_rtnl;
	ifindex = dev->ifindex;
	dev_put(dev);

	if (!drv->ops->del_virtual_intf) {
		err = -EOPNOTSUPP;
		goto out;
	}

	err = drv->ops->del_virtual_intf(&drv->wiphy, ifindex);

 out:
	cfg80211_put_dev(drv);
 unlock_rtnl:
	rtnl_unlock();
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

	if (key_idx > 5)
		return -EINVAL;

	if (info->attrs[NL80211_ATTR_MAC])
		mac_addr = nla_data(info->attrs[NL80211_ATTR_MAC]);

	rtnl_lock();

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		goto unlock_rtnl;

	if (!drv->ops->get_key) {
		err = -EOPNOTSUPP;
		goto out;
	}

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		err = -ENOMEM;
		goto out;
	}

	hdr = nl80211hdr_put(msg, info->snd_pid, info->snd_seq, 0,
			     NL80211_CMD_NEW_KEY);

	if (IS_ERR(hdr)) {
		err = PTR_ERR(hdr);
		goto free_msg;
	}

	cookie.msg = msg;

	NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, dev->ifindex);
	NLA_PUT_U8(msg, NL80211_ATTR_KEY_IDX, key_idx);
	if (mac_addr)
		NLA_PUT(msg, NL80211_ATTR_MAC, ETH_ALEN, mac_addr);

	err = drv->ops->get_key(&drv->wiphy, dev, key_idx, mac_addr,
				&cookie, get_key_callback);

	if (err)
		goto free_msg;

	if (cookie.error)
		goto nla_put_failure;

	genlmsg_end(msg, hdr);
	err = genlmsg_unicast(msg, info->snd_pid);
	goto out;

 nla_put_failure:
	err = -ENOBUFS;
 free_msg:
	nlmsg_free(msg);
 out:
	cfg80211_put_dev(drv);
	dev_put(dev);
 unlock_rtnl:
	rtnl_unlock();

	return err;
}

static int nl80211_set_key(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *drv;
	int err;
	struct net_device *dev;
	u8 key_idx;
	int (*func)(struct wiphy *wiphy, struct net_device *netdev,
		    u8 key_index);

	if (!info->attrs[NL80211_ATTR_KEY_IDX])
		return -EINVAL;

	key_idx = nla_get_u8(info->attrs[NL80211_ATTR_KEY_IDX]);

	if (info->attrs[NL80211_ATTR_KEY_DEFAULT_MGMT]) {
		if (key_idx < 4 || key_idx > 5)
			return -EINVAL;
	} else if (key_idx > 3)
		return -EINVAL;

	/* currently only support setting default key */
	if (!info->attrs[NL80211_ATTR_KEY_DEFAULT] &&
	    !info->attrs[NL80211_ATTR_KEY_DEFAULT_MGMT])
		return -EINVAL;

	rtnl_lock();

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		goto unlock_rtnl;

	if (info->attrs[NL80211_ATTR_KEY_DEFAULT])
		func = drv->ops->set_default_key;
	else
		func = drv->ops->set_default_mgmt_key;

	if (!func) {
		err = -EOPNOTSUPP;
		goto out;
	}

	err = func(&drv->wiphy, dev, key_idx);
#ifdef CONFIG_WIRELESS_EXT
	if (!err) {
		if (func == drv->ops->set_default_key)
			dev->ieee80211_ptr->wext.default_key = key_idx;
		else
			dev->ieee80211_ptr->wext.default_mgmt_key = key_idx;
	}
#endif

 out:
	cfg80211_put_dev(drv);
	dev_put(dev);

 unlock_rtnl:
	rtnl_unlock();

	return err;
}

static int nl80211_new_key(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *drv;
	int err, i;
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

	if (info->attrs[NL80211_ATTR_KEY_SEQ]) {
		params.seq = nla_data(info->attrs[NL80211_ATTR_KEY_SEQ]);
		params.seq_len = nla_len(info->attrs[NL80211_ATTR_KEY_SEQ]);
	}

	if (info->attrs[NL80211_ATTR_KEY_IDX])
		key_idx = nla_get_u8(info->attrs[NL80211_ATTR_KEY_IDX]);

	params.cipher = nla_get_u32(info->attrs[NL80211_ATTR_KEY_CIPHER]);

	if (info->attrs[NL80211_ATTR_MAC])
		mac_addr = nla_data(info->attrs[NL80211_ATTR_MAC]);

	if (cfg80211_validate_key_settings(&params, key_idx, mac_addr))
		return -EINVAL;

	rtnl_lock();

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		goto unlock_rtnl;

	for (i = 0; i < drv->wiphy.n_cipher_suites; i++)
		if (params.cipher == drv->wiphy.cipher_suites[i])
			break;
	if (i == drv->wiphy.n_cipher_suites) {
		err = -EINVAL;
		goto out;
	}

	if (!drv->ops->add_key) {
		err = -EOPNOTSUPP;
		goto out;
	}

	err = drv->ops->add_key(&drv->wiphy, dev, key_idx, mac_addr, &params);

 out:
	cfg80211_put_dev(drv);
	dev_put(dev);
 unlock_rtnl:
	rtnl_unlock();

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

	if (key_idx > 5)
		return -EINVAL;

	if (info->attrs[NL80211_ATTR_MAC])
		mac_addr = nla_data(info->attrs[NL80211_ATTR_MAC]);

	rtnl_lock();

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		goto unlock_rtnl;

	if (!drv->ops->del_key) {
		err = -EOPNOTSUPP;
		goto out;
	}

	err = drv->ops->del_key(&drv->wiphy, dev, key_idx, mac_addr);

#ifdef CONFIG_WIRELESS_EXT
	if (!err) {
		if (key_idx == dev->ieee80211_ptr->wext.default_key)
			dev->ieee80211_ptr->wext.default_key = -1;
		else if (key_idx == dev->ieee80211_ptr->wext.default_mgmt_key)
			dev->ieee80211_ptr->wext.default_mgmt_key = -1;
	}
#endif

 out:
	cfg80211_put_dev(drv);
	dev_put(dev);

 unlock_rtnl:
	rtnl_unlock();

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

	if (!is_valid_ie_attr(info->attrs[NL80211_ATTR_BEACON_TAIL]))
		return -EINVAL;

	rtnl_lock();

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		goto unlock_rtnl;

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_AP) {
		err = -EOPNOTSUPP;
		goto out;
	}

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

	err = call(&drv->wiphy, dev, &params);

 out:
	cfg80211_put_dev(drv);
	dev_put(dev);
 unlock_rtnl:
	rtnl_unlock();

	return err;
}

static int nl80211_del_beacon(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *drv;
	int err;
	struct net_device *dev;

	rtnl_lock();

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		goto unlock_rtnl;

	if (!drv->ops->del_beacon) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_AP) {
		err = -EOPNOTSUPP;
		goto out;
	}
	err = drv->ops->del_beacon(&drv->wiphy, dev);

 out:
	cfg80211_put_dev(drv);
	dev_put(dev);
 unlock_rtnl:
	rtnl_unlock();

	return err;
}

static const struct nla_policy sta_flags_policy[NL80211_STA_FLAG_MAX + 1] = {
	[NL80211_STA_FLAG_AUTHORIZED] = { .type = NLA_FLAG },
	[NL80211_STA_FLAG_SHORT_PREAMBLE] = { .type = NLA_FLAG },
	[NL80211_STA_FLAG_WME] = { .type = NLA_FLAG },
	[NL80211_STA_FLAG_MFP] = { .type = NLA_FLAG },
};

static int parse_station_flags(struct genl_info *info,
			       struct station_parameters *params)
{
	struct nlattr *flags[NL80211_STA_FLAG_MAX + 1];
	struct nlattr *nla;
	int flag;

	/*
	 * Try parsing the new attribute first so userspace
	 * can specify both for older kernels.
	 */
	nla = info->attrs[NL80211_ATTR_STA_FLAGS2];
	if (nla) {
		struct nl80211_sta_flag_update *sta_flags;

		sta_flags = nla_data(nla);
		params->sta_flags_mask = sta_flags->mask;
		params->sta_flags_set = sta_flags->set;
		if ((params->sta_flags_mask |
		     params->sta_flags_set) & BIT(__NL80211_STA_FLAG_INVALID))
			return -EINVAL;
		return 0;
	}

	/* if present, parse the old attribute */

	nla = info->attrs[NL80211_ATTR_STA_FLAGS];
	if (!nla)
		return 0;

	if (nla_parse_nested(flags, NL80211_STA_FLAG_MAX,
			     nla, sta_flags_policy))
		return -EINVAL;

	params->sta_flags_mask = (1 << __NL80211_STA_FLAG_AFTER_LAST) - 1;
	params->sta_flags_mask &= ~1;

	for (flag = 1; flag <= NL80211_STA_FLAG_MAX; flag++)
		if (flags[flag])
			params->sta_flags_set |= (1<<flag);

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
	if (sinfo->filled & STATION_INFO_RX_PACKETS)
		NLA_PUT_U32(msg, NL80211_STA_INFO_RX_PACKETS,
			    sinfo->rx_packets);
	if (sinfo->filled & STATION_INFO_TX_PACKETS)
		NLA_PUT_U32(msg, NL80211_STA_INFO_TX_PACKETS,
			    sinfo->tx_packets);
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

	rtnl_lock();

	netdev = __dev_get_by_index(&init_net, ifidx);
	if (!netdev) {
		err = -ENODEV;
		goto out_rtnl;
	}

	dev = cfg80211_get_dev_from_ifindex(ifidx);
	if (IS_ERR(dev)) {
		err = PTR_ERR(dev);
		goto out_rtnl;
	}

	if (!dev->ops->dump_station) {
		err = -EOPNOTSUPP;
		goto out_err;
	}

	while (1) {
		err = dev->ops->dump_station(&dev->wiphy, netdev, sta_idx,
					     mac_addr, &sinfo);
		if (err == -ENOENT)
			break;
		if (err)
			goto out_err;

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
 out_err:
	cfg80211_put_dev(dev);
 out_rtnl:
	rtnl_unlock();

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

	rtnl_lock();

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		goto out_rtnl;

	if (!drv->ops->get_station) {
		err = -EOPNOTSUPP;
		goto out;
	}

	err = drv->ops->get_station(&drv->wiphy, dev, mac_addr, &sinfo);
	if (err)
		goto out;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
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
 out_rtnl:
	rtnl_unlock();

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

	if (parse_station_flags(info, &params))
		return -EINVAL;

	if (info->attrs[NL80211_ATTR_STA_PLINK_ACTION])
		params.plink_action =
		    nla_get_u8(info->attrs[NL80211_ATTR_STA_PLINK_ACTION]);

	rtnl_lock();

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		goto out_rtnl;

	err = get_vlan(info->attrs[NL80211_ATTR_STA_VLAN], drv, &params.vlan);
	if (err)
		goto out;

	/* validate settings */
	err = 0;

	switch (dev->ieee80211_ptr->iftype) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_AP_VLAN:
		/* disallow mesh-specific things */
		if (params.plink_action)
			err = -EINVAL;
		break;
	case NL80211_IFTYPE_STATION:
		/* disallow everything but AUTHORIZED flag */
		if (params.plink_action)
			err = -EINVAL;
		if (params.vlan)
			err = -EINVAL;
		if (params.supported_rates)
			err = -EINVAL;
		if (params.ht_capa)
			err = -EINVAL;
		if (params.listen_interval >= 0)
			err = -EINVAL;
		if (params.sta_flags_mask & ~BIT(NL80211_STA_FLAG_AUTHORIZED))
			err = -EINVAL;
		break;
	case NL80211_IFTYPE_MESH_POINT:
		/* disallow things mesh doesn't support */
		if (params.vlan)
			err = -EINVAL;
		if (params.ht_capa)
			err = -EINVAL;
		if (params.listen_interval >= 0)
			err = -EINVAL;
		if (params.supported_rates)
			err = -EINVAL;
		if (params.sta_flags_mask)
			err = -EINVAL;
		break;
	default:
		err = -EINVAL;
	}

	if (err)
		goto out;

	if (!drv->ops->change_station) {
		err = -EOPNOTSUPP;
		goto out;
	}

	err = drv->ops->change_station(&drv->wiphy, dev, mac_addr, &params);

 out:
	if (params.vlan)
		dev_put(params.vlan);
	cfg80211_put_dev(drv);
	dev_put(dev);
 out_rtnl:
	rtnl_unlock();

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

	if (info->attrs[NL80211_ATTR_STA_AID]) {
		params.aid = nla_get_u16(info->attrs[NL80211_ATTR_STA_AID]);
		if (!params.aid || params.aid > IEEE80211_MAX_AID)
			return -EINVAL;
	}

	if (info->attrs[NL80211_ATTR_HT_CAPABILITY])
		params.ht_capa =
			nla_data(info->attrs[NL80211_ATTR_HT_CAPABILITY]);

	if (parse_station_flags(info, &params))
		return -EINVAL;

	rtnl_lock();

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		goto out_rtnl;

	err = get_vlan(info->attrs[NL80211_ATTR_STA_VLAN], drv, &params.vlan);
	if (err)
		goto out;

	/* validate settings */
	err = 0;

	switch (dev->ieee80211_ptr->iftype) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_AP_VLAN:
		/* all ok but must have AID */
		if (!params.aid)
			err = -EINVAL;
		break;
	case NL80211_IFTYPE_MESH_POINT:
		/* disallow things mesh doesn't support */
		if (params.vlan)
			err = -EINVAL;
		if (params.aid)
			err = -EINVAL;
		if (params.ht_capa)
			err = -EINVAL;
		if (params.listen_interval >= 0)
			err = -EINVAL;
		if (params.supported_rates)
			err = -EINVAL;
		if (params.sta_flags_mask)
			err = -EINVAL;
		break;
	default:
		err = -EINVAL;
	}

	if (err)
		goto out;

	if (!drv->ops->add_station) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (!netif_running(dev)) {
		err = -ENETDOWN;
		goto out;
	}

	err = drv->ops->add_station(&drv->wiphy, dev, mac_addr, &params);

 out:
	if (params.vlan)
		dev_put(params.vlan);
	cfg80211_put_dev(drv);
	dev_put(dev);
 out_rtnl:
	rtnl_unlock();

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

	rtnl_lock();

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		goto out_rtnl;

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_AP &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_AP_VLAN &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_MESH_POINT) {
		err = -EINVAL;
		goto out;
	}

	if (!drv->ops->del_station) {
		err = -EOPNOTSUPP;
		goto out;
	}

	err = drv->ops->del_station(&drv->wiphy, dev, mac_addr);

 out:
	cfg80211_put_dev(drv);
	dev_put(dev);
 out_rtnl:
	rtnl_unlock();

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

	rtnl_lock();

	netdev = __dev_get_by_index(&init_net, ifidx);
	if (!netdev) {
		err = -ENODEV;
		goto out_rtnl;
	}

	dev = cfg80211_get_dev_from_ifindex(ifidx);
	if (IS_ERR(dev)) {
		err = PTR_ERR(dev);
		goto out_rtnl;
	}

	if (!dev->ops->dump_mpath) {
		err = -EOPNOTSUPP;
		goto out_err;
	}

	if (netdev->ieee80211_ptr->iftype != NL80211_IFTYPE_MESH_POINT) {
		err = -EOPNOTSUPP;
		goto out;
	}

	while (1) {
		err = dev->ops->dump_mpath(&dev->wiphy, netdev, path_idx,
					   dst, next_hop, &pinfo);
		if (err == -ENOENT)
			break;
		if (err)
			goto out_err;

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
 out_err:
	cfg80211_put_dev(dev);
 out_rtnl:
	rtnl_unlock();

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

	rtnl_lock();

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		goto out_rtnl;

	if (!drv->ops->get_mpath) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_MESH_POINT) {
		err = -EOPNOTSUPP;
		goto out;
	}

	err = drv->ops->get_mpath(&drv->wiphy, dev, dst, next_hop, &pinfo);
	if (err)
		goto out;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
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
 out_rtnl:
	rtnl_unlock();

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

	rtnl_lock();

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		goto out_rtnl;

	if (!drv->ops->change_mpath) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_MESH_POINT) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (!netif_running(dev)) {
		err = -ENETDOWN;
		goto out;
	}

	err = drv->ops->change_mpath(&drv->wiphy, dev, dst, next_hop);

 out:
	cfg80211_put_dev(drv);
	dev_put(dev);
 out_rtnl:
	rtnl_unlock();

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

	rtnl_lock();

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		goto out_rtnl;

	if (!drv->ops->add_mpath) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_MESH_POINT) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (!netif_running(dev)) {
		err = -ENETDOWN;
		goto out;
	}

	err = drv->ops->add_mpath(&drv->wiphy, dev, dst, next_hop);

 out:
	cfg80211_put_dev(drv);
	dev_put(dev);
 out_rtnl:
	rtnl_unlock();

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

	rtnl_lock();

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		goto out_rtnl;

	if (!drv->ops->del_mpath) {
		err = -EOPNOTSUPP;
		goto out;
	}

	err = drv->ops->del_mpath(&drv->wiphy, dev, dst);

 out:
	cfg80211_put_dev(drv);
	dev_put(dev);
 out_rtnl:
	rtnl_unlock();

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

	rtnl_lock();

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		goto out_rtnl;

	if (!drv->ops->change_bss) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_AP) {
		err = -EOPNOTSUPP;
		goto out;
	}

	err = drv->ops->change_bss(&drv->wiphy, dev, &params);

 out:
	cfg80211_put_dev(drv);
	dev_put(dev);
 out_rtnl:
	rtnl_unlock();

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

	/*
	 * You should only get this when cfg80211 hasn't yet initialized
	 * completely when built-in to the kernel right between the time
	 * window between nl80211_init() and regulatory_init(), if that is
	 * even possible.
	 */
	mutex_lock(&cfg80211_mutex);
	if (unlikely(!cfg80211_regdomain)) {
		mutex_unlock(&cfg80211_mutex);
		return -EINPROGRESS;
	}
	mutex_unlock(&cfg80211_mutex);

	if (!info->attrs[NL80211_ATTR_REG_ALPHA2])
		return -EINVAL;

	data = nla_data(info->attrs[NL80211_ATTR_REG_ALPHA2]);

#ifdef CONFIG_WIRELESS_OLD_REGULATORY
	/* We ignore world regdom requests with the old regdom setup */
	if (is_world_regdom(data))
		return -EINVAL;
#endif

	r = regulatory_hint_user(data);

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

	rtnl_lock();

	/* Look up our device */
	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		goto out_rtnl;

	if (!drv->ops->get_mesh_params) {
		err = -EOPNOTSUPP;
		goto out;
	}

	/* Get the mesh params */
	err = drv->ops->get_mesh_params(&drv->wiphy, dev, &cur_params);
	if (err)
		goto out;

	/* Draw up a netlink message to send back */
	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
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
 out_rtnl:
	rtnl_unlock();

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

	rtnl_lock();

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		goto out_rtnl;

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
	err = drv->ops->set_mesh_params(&drv->wiphy, dev, &cfg, mask);

 out:
	/* cleanup */
	cfg80211_put_dev(drv);
	dev_put(dev);
 out_rtnl:
	rtnl_unlock();

	return err;
}

#undef FILL_IN_MESH_PARAM_IF_SET

static int nl80211_get_reg(struct sk_buff *skb, struct genl_info *info)
{
	struct sk_buff *msg;
	void *hdr = NULL;
	struct nlattr *nl_reg_rules;
	unsigned int i;
	int err = -EINVAL;

	mutex_lock(&cfg80211_mutex);

	if (!cfg80211_regdomain)
		goto out;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		err = -ENOBUFS;
		goto out;
	}

	hdr = nl80211hdr_put(msg, info->snd_pid, info->snd_seq, 0,
			     NL80211_CMD_GET_REG);
	if (!hdr)
		goto nla_put_failure;

	NLA_PUT_STRING(msg, NL80211_ATTR_REG_ALPHA2,
		cfg80211_regdomain->alpha2);

	nl_reg_rules = nla_nest_start(msg, NL80211_ATTR_REG_RULES);
	if (!nl_reg_rules)
		goto nla_put_failure;

	for (i = 0; i < cfg80211_regdomain->n_reg_rules; i++) {
		struct nlattr *nl_reg_rule;
		const struct ieee80211_reg_rule *reg_rule;
		const struct ieee80211_freq_range *freq_range;
		const struct ieee80211_power_rule *power_rule;

		reg_rule = &cfg80211_regdomain->reg_rules[i];
		freq_range = &reg_rule->freq_range;
		power_rule = &reg_rule->power_rule;

		nl_reg_rule = nla_nest_start(msg, i);
		if (!nl_reg_rule)
			goto nla_put_failure;

		NLA_PUT_U32(msg, NL80211_ATTR_REG_RULE_FLAGS,
			reg_rule->flags);
		NLA_PUT_U32(msg, NL80211_ATTR_FREQ_RANGE_START,
			freq_range->start_freq_khz);
		NLA_PUT_U32(msg, NL80211_ATTR_FREQ_RANGE_END,
			freq_range->end_freq_khz);
		NLA_PUT_U32(msg, NL80211_ATTR_FREQ_RANGE_MAX_BW,
			freq_range->max_bandwidth_khz);
		NLA_PUT_U32(msg, NL80211_ATTR_POWER_RULE_MAX_ANT_GAIN,
			power_rule->max_antenna_gain);
		NLA_PUT_U32(msg, NL80211_ATTR_POWER_RULE_MAX_EIRP,
			power_rule->max_eirp);

		nla_nest_end(msg, nl_reg_rule);
	}

	nla_nest_end(msg, nl_reg_rules);

	genlmsg_end(msg, hdr);
	err = genlmsg_unicast(msg, info->snd_pid);
	goto out;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	err = -EMSGSIZE;
out:
	mutex_unlock(&cfg80211_mutex);
	return err;
}

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
			return -EINVAL;
	}

	mutex_lock(&cfg80211_mutex);

	if (!reg_is_valid_request(alpha2)) {
		r = -EINVAL;
		goto bad_reg;
	}

	size_of_regd = sizeof(struct ieee80211_regdomain) +
		(num_rules * sizeof(struct ieee80211_reg_rule));

	rd = kzalloc(size_of_regd, GFP_KERNEL);
	if (!rd) {
		r = -ENOMEM;
		goto bad_reg;
	}

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

		if (rule_idx > NL80211_MAX_SUPP_REG_RULES) {
			r = -EINVAL;
			goto bad_reg;
		}
	}

	BUG_ON(rule_idx != num_rules);

	r = set_regdom(rd);

	mutex_unlock(&cfg80211_mutex);

	return r;

 bad_reg:
	mutex_unlock(&cfg80211_mutex);
	kfree(rd);
	return r;
}

static int nl80211_trigger_scan(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *drv;
	struct net_device *dev;
	struct cfg80211_scan_request *request;
	struct cfg80211_ssid *ssid;
	struct ieee80211_channel *channel;
	struct nlattr *attr;
	struct wiphy *wiphy;
	int err, tmp, n_ssids = 0, n_channels = 0, i;
	enum ieee80211_band band;
	size_t ie_len;

	if (!is_valid_ie_attr(info->attrs[NL80211_ATTR_IE]))
		return -EINVAL;

	rtnl_lock();

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		goto out_rtnl;

	wiphy = &drv->wiphy;

	if (!drv->ops->scan) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (!netif_running(dev)) {
		err = -ENETDOWN;
		goto out;
	}

	if (drv->scan_req) {
		err = -EBUSY;
		goto out;
	}

	if (info->attrs[NL80211_ATTR_SCAN_FREQUENCIES]) {
		nla_for_each_nested(attr, info->attrs[NL80211_ATTR_SCAN_FREQUENCIES], tmp)
			n_channels++;
		if (!n_channels) {
			err = -EINVAL;
			goto out;
		}
	} else {
		for (band = 0; band < IEEE80211_NUM_BANDS; band++)
			if (wiphy->bands[band])
				n_channels += wiphy->bands[band]->n_channels;
	}

	if (info->attrs[NL80211_ATTR_SCAN_SSIDS])
		nla_for_each_nested(attr, info->attrs[NL80211_ATTR_SCAN_SSIDS], tmp)
			n_ssids++;

	if (n_ssids > wiphy->max_scan_ssids) {
		err = -EINVAL;
		goto out;
	}

	if (info->attrs[NL80211_ATTR_IE])
		ie_len = nla_len(info->attrs[NL80211_ATTR_IE]);
	else
		ie_len = 0;

	if (ie_len > wiphy->max_scan_ie_len) {
		err = -EINVAL;
		goto out;
	}

	request = kzalloc(sizeof(*request)
			+ sizeof(*ssid) * n_ssids
			+ sizeof(channel) * n_channels
			+ ie_len, GFP_KERNEL);
	if (!request) {
		err = -ENOMEM;
		goto out;
	}

	request->channels = (void *)((char *)request + sizeof(*request));
	request->n_channels = n_channels;
	if (n_ssids)
		request->ssids = (void *)(request->channels + n_channels);
	request->n_ssids = n_ssids;
	if (ie_len) {
		if (request->ssids)
			request->ie = (void *)(request->ssids + n_ssids);
		else
			request->ie = (void *)(request->channels + n_channels);
	}

	if (info->attrs[NL80211_ATTR_SCAN_FREQUENCIES]) {
		/* user specified, bail out if channel not found */
		request->n_channels = n_channels;
		i = 0;
		nla_for_each_nested(attr, info->attrs[NL80211_ATTR_SCAN_FREQUENCIES], tmp) {
			request->channels[i] = ieee80211_get_channel(wiphy, nla_get_u32(attr));
			if (!request->channels[i]) {
				err = -EINVAL;
				goto out_free;
			}
			i++;
		}
	} else {
		/* all channels */
		i = 0;
		for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
			int j;
			if (!wiphy->bands[band])
				continue;
			for (j = 0; j < wiphy->bands[band]->n_channels; j++) {
				request->channels[i] = &wiphy->bands[band]->channels[j];
				i++;
			}
		}
	}

	i = 0;
	if (info->attrs[NL80211_ATTR_SCAN_SSIDS]) {
		nla_for_each_nested(attr, info->attrs[NL80211_ATTR_SCAN_SSIDS], tmp) {
			if (request->ssids[i].ssid_len > IEEE80211_MAX_SSID_LEN) {
				err = -EINVAL;
				goto out_free;
			}
			memcpy(request->ssids[i].ssid, nla_data(attr), nla_len(attr));
			request->ssids[i].ssid_len = nla_len(attr);
			i++;
		}
	}

	if (info->attrs[NL80211_ATTR_IE]) {
		request->ie_len = nla_len(info->attrs[NL80211_ATTR_IE]);
		memcpy((void *)request->ie,
		       nla_data(info->attrs[NL80211_ATTR_IE]),
		       request->ie_len);
	}

	request->ifidx = dev->ifindex;
	request->wiphy = &drv->wiphy;

	drv->scan_req = request;
	err = drv->ops->scan(&drv->wiphy, dev, request);

 out_free:
	if (err) {
		drv->scan_req = NULL;
		kfree(request);
	}
 out:
	cfg80211_put_dev(drv);
	dev_put(dev);
 out_rtnl:
	rtnl_unlock();

	return err;
}

static int nl80211_send_bss(struct sk_buff *msg, u32 pid, u32 seq, int flags,
			    struct cfg80211_registered_device *rdev,
			    struct net_device *dev,
			    struct cfg80211_bss *res)
{
	void *hdr;
	struct nlattr *bss;

	hdr = nl80211hdr_put(msg, pid, seq, flags,
			     NL80211_CMD_NEW_SCAN_RESULTS);
	if (!hdr)
		return -1;

	NLA_PUT_U32(msg, NL80211_ATTR_SCAN_GENERATION,
		    rdev->bss_generation);
	NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, dev->ifindex);

	bss = nla_nest_start(msg, NL80211_ATTR_BSS);
	if (!bss)
		goto nla_put_failure;
	if (!is_zero_ether_addr(res->bssid))
		NLA_PUT(msg, NL80211_BSS_BSSID, ETH_ALEN, res->bssid);
	if (res->information_elements && res->len_information_elements)
		NLA_PUT(msg, NL80211_BSS_INFORMATION_ELEMENTS,
			res->len_information_elements,
			res->information_elements);
	if (res->tsf)
		NLA_PUT_U64(msg, NL80211_BSS_TSF, res->tsf);
	if (res->beacon_interval)
		NLA_PUT_U16(msg, NL80211_BSS_BEACON_INTERVAL, res->beacon_interval);
	NLA_PUT_U16(msg, NL80211_BSS_CAPABILITY, res->capability);
	NLA_PUT_U32(msg, NL80211_BSS_FREQUENCY, res->channel->center_freq);

	switch (rdev->wiphy.signal_type) {
	case CFG80211_SIGNAL_TYPE_MBM:
		NLA_PUT_U32(msg, NL80211_BSS_SIGNAL_MBM, res->signal);
		break;
	case CFG80211_SIGNAL_TYPE_UNSPEC:
		NLA_PUT_U8(msg, NL80211_BSS_SIGNAL_UNSPEC, res->signal);
		break;
	default:
		break;
	}

	nla_nest_end(msg, bss);

	return genlmsg_end(msg, hdr);

 nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int nl80211_dump_scan(struct sk_buff *skb,
			     struct netlink_callback *cb)
{
	struct cfg80211_registered_device *dev;
	struct net_device *netdev;
	struct cfg80211_internal_bss *scan;
	int ifidx = cb->args[0];
	int start = cb->args[1], idx = 0;
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
		cb->args[0] = ifidx;
	}

	netdev = dev_get_by_index(&init_net, ifidx);
	if (!netdev)
		return -ENODEV;

	dev = cfg80211_get_dev_from_ifindex(ifidx);
	if (IS_ERR(dev)) {
		err = PTR_ERR(dev);
		goto out_put_netdev;
	}

	spin_lock_bh(&dev->bss_lock);
	cfg80211_bss_expire(dev);

	list_for_each_entry(scan, &dev->bss_list, list) {
		if (++idx <= start)
			continue;
		if (nl80211_send_bss(skb,
				NETLINK_CB(cb->skb).pid,
				cb->nlh->nlmsg_seq, NLM_F_MULTI,
				dev, netdev, &scan->pub) < 0) {
			idx--;
			goto out;
		}
	}

 out:
	spin_unlock_bh(&dev->bss_lock);

	cb->args[1] = idx;
	err = skb->len;
	cfg80211_put_dev(dev);
 out_put_netdev:
	dev_put(netdev);

	return err;
}

static bool nl80211_valid_auth_type(enum nl80211_auth_type auth_type)
{
	return auth_type == NL80211_AUTHTYPE_OPEN_SYSTEM ||
		auth_type == NL80211_AUTHTYPE_SHARED_KEY ||
		auth_type == NL80211_AUTHTYPE_FT ||
		auth_type == NL80211_AUTHTYPE_NETWORK_EAP;
}

static int nl80211_authenticate(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *drv;
	struct net_device *dev;
	struct cfg80211_auth_request req;
	struct wiphy *wiphy;
	int err;

	if (!is_valid_ie_attr(info->attrs[NL80211_ATTR_IE]))
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_MAC])
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_AUTH_TYPE])
		return -EINVAL;

	rtnl_lock();

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		goto unlock_rtnl;

	if (!drv->ops->auth) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_STATION) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (!netif_running(dev)) {
		err = -ENETDOWN;
		goto out;
	}

	wiphy = &drv->wiphy;
	memset(&req, 0, sizeof(req));

	req.peer_addr = nla_data(info->attrs[NL80211_ATTR_MAC]);

	if (info->attrs[NL80211_ATTR_WIPHY_FREQ]) {
		req.chan = ieee80211_get_channel(
			wiphy,
			nla_get_u32(info->attrs[NL80211_ATTR_WIPHY_FREQ]));
		if (!req.chan) {
			err = -EINVAL;
			goto out;
		}
	}

	if (info->attrs[NL80211_ATTR_SSID]) {
		req.ssid = nla_data(info->attrs[NL80211_ATTR_SSID]);
		req.ssid_len = nla_len(info->attrs[NL80211_ATTR_SSID]);
	}

	if (info->attrs[NL80211_ATTR_IE]) {
		req.ie = nla_data(info->attrs[NL80211_ATTR_IE]);
		req.ie_len = nla_len(info->attrs[NL80211_ATTR_IE]);
	}

	req.auth_type = nla_get_u32(info->attrs[NL80211_ATTR_AUTH_TYPE]);
	if (!nl80211_valid_auth_type(req.auth_type)) {
		err = -EINVAL;
		goto out;
	}

	err = drv->ops->auth(&drv->wiphy, dev, &req);

out:
	cfg80211_put_dev(drv);
	dev_put(dev);
unlock_rtnl:
	rtnl_unlock();
	return err;
}

static int nl80211_associate(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *drv;
	struct net_device *dev;
	struct cfg80211_assoc_request req;
	struct wiphy *wiphy;
	int err;

	if (!is_valid_ie_attr(info->attrs[NL80211_ATTR_IE]))
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_MAC] ||
	    !info->attrs[NL80211_ATTR_SSID])
		return -EINVAL;

	rtnl_lock();

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		goto unlock_rtnl;

	if (!drv->ops->assoc) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_STATION) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (!netif_running(dev)) {
		err = -ENETDOWN;
		goto out;
	}

	wiphy = &drv->wiphy;
	memset(&req, 0, sizeof(req));

	req.peer_addr = nla_data(info->attrs[NL80211_ATTR_MAC]);

	if (info->attrs[NL80211_ATTR_WIPHY_FREQ]) {
		req.chan = ieee80211_get_channel(
			wiphy,
			nla_get_u32(info->attrs[NL80211_ATTR_WIPHY_FREQ]));
		if (!req.chan) {
			err = -EINVAL;
			goto out;
		}
	}

	req.ssid = nla_data(info->attrs[NL80211_ATTR_SSID]);
	req.ssid_len = nla_len(info->attrs[NL80211_ATTR_SSID]);

	if (info->attrs[NL80211_ATTR_IE]) {
		req.ie = nla_data(info->attrs[NL80211_ATTR_IE]);
		req.ie_len = nla_len(info->attrs[NL80211_ATTR_IE]);
	}

	if (info->attrs[NL80211_ATTR_USE_MFP]) {
		enum nl80211_mfp use_mfp =
			nla_get_u32(info->attrs[NL80211_ATTR_USE_MFP]);
		if (use_mfp == NL80211_MFP_REQUIRED)
			req.use_mfp = true;
		else if (use_mfp != NL80211_MFP_NO) {
			err = -EINVAL;
			goto out;
		}
	}

	req.control_port = info->attrs[NL80211_ATTR_CONTROL_PORT];

	err = drv->ops->assoc(&drv->wiphy, dev, &req);

out:
	cfg80211_put_dev(drv);
	dev_put(dev);
unlock_rtnl:
	rtnl_unlock();
	return err;
}

static int nl80211_deauthenticate(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *drv;
	struct net_device *dev;
	struct cfg80211_deauth_request req;
	struct wiphy *wiphy;
	int err;

	if (!is_valid_ie_attr(info->attrs[NL80211_ATTR_IE]))
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_MAC])
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_REASON_CODE])
		return -EINVAL;

	rtnl_lock();

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		goto unlock_rtnl;

	if (!drv->ops->deauth) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_STATION) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (!netif_running(dev)) {
		err = -ENETDOWN;
		goto out;
	}

	wiphy = &drv->wiphy;
	memset(&req, 0, sizeof(req));

	req.peer_addr = nla_data(info->attrs[NL80211_ATTR_MAC]);

	req.reason_code = nla_get_u16(info->attrs[NL80211_ATTR_REASON_CODE]);
	if (req.reason_code == 0) {
		/* Reason Code 0 is reserved */
		err = -EINVAL;
		goto out;
	}

	if (info->attrs[NL80211_ATTR_IE]) {
		req.ie = nla_data(info->attrs[NL80211_ATTR_IE]);
		req.ie_len = nla_len(info->attrs[NL80211_ATTR_IE]);
	}

	err = drv->ops->deauth(&drv->wiphy, dev, &req);

out:
	cfg80211_put_dev(drv);
	dev_put(dev);
unlock_rtnl:
	rtnl_unlock();
	return err;
}

static int nl80211_disassociate(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *drv;
	struct net_device *dev;
	struct cfg80211_disassoc_request req;
	struct wiphy *wiphy;
	int err;

	if (!is_valid_ie_attr(info->attrs[NL80211_ATTR_IE]))
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_MAC])
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_REASON_CODE])
		return -EINVAL;

	rtnl_lock();

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		goto unlock_rtnl;

	if (!drv->ops->disassoc) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_STATION) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (!netif_running(dev)) {
		err = -ENETDOWN;
		goto out;
	}

	wiphy = &drv->wiphy;
	memset(&req, 0, sizeof(req));

	req.peer_addr = nla_data(info->attrs[NL80211_ATTR_MAC]);

	req.reason_code = nla_get_u16(info->attrs[NL80211_ATTR_REASON_CODE]);
	if (req.reason_code == 0) {
		/* Reason Code 0 is reserved */
		err = -EINVAL;
		goto out;
	}

	if (info->attrs[NL80211_ATTR_IE]) {
		req.ie = nla_data(info->attrs[NL80211_ATTR_IE]);
		req.ie_len = nla_len(info->attrs[NL80211_ATTR_IE]);
	}

	err = drv->ops->disassoc(&drv->wiphy, dev, &req);

out:
	cfg80211_put_dev(drv);
	dev_put(dev);
unlock_rtnl:
	rtnl_unlock();
	return err;
}

static int nl80211_join_ibss(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *drv;
	struct net_device *dev;
	struct cfg80211_ibss_params ibss;
	struct wiphy *wiphy;
	int err;

	memset(&ibss, 0, sizeof(ibss));

	if (!is_valid_ie_attr(info->attrs[NL80211_ATTR_IE]))
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_WIPHY_FREQ] ||
	    !info->attrs[NL80211_ATTR_SSID] ||
	    !nla_len(info->attrs[NL80211_ATTR_SSID]))
		return -EINVAL;

	ibss.beacon_interval = 100;

	if (info->attrs[NL80211_ATTR_BEACON_INTERVAL]) {
		ibss.beacon_interval =
			nla_get_u32(info->attrs[NL80211_ATTR_BEACON_INTERVAL]);
		if (ibss.beacon_interval < 1 || ibss.beacon_interval > 10000)
			return -EINVAL;
	}

	rtnl_lock();

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		goto unlock_rtnl;

	if (!drv->ops->join_ibss) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_ADHOC) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (!netif_running(dev)) {
		err = -ENETDOWN;
		goto out;
	}

	wiphy = &drv->wiphy;

	if (info->attrs[NL80211_ATTR_MAC])
		ibss.bssid = nla_data(info->attrs[NL80211_ATTR_MAC]);
	ibss.ssid = nla_data(info->attrs[NL80211_ATTR_SSID]);
	ibss.ssid_len = nla_len(info->attrs[NL80211_ATTR_SSID]);

	if (info->attrs[NL80211_ATTR_IE]) {
		ibss.ie = nla_data(info->attrs[NL80211_ATTR_IE]);
		ibss.ie_len = nla_len(info->attrs[NL80211_ATTR_IE]);
	}

	ibss.channel = ieee80211_get_channel(wiphy,
		nla_get_u32(info->attrs[NL80211_ATTR_WIPHY_FREQ]));
	if (!ibss.channel ||
	    ibss.channel->flags & IEEE80211_CHAN_NO_IBSS ||
	    ibss.channel->flags & IEEE80211_CHAN_DISABLED) {
		err = -EINVAL;
		goto out;
	}

	ibss.channel_fixed = !!info->attrs[NL80211_ATTR_FREQ_FIXED];

	err = cfg80211_join_ibss(drv, dev, &ibss);

out:
	cfg80211_put_dev(drv);
	dev_put(dev);
unlock_rtnl:
	rtnl_unlock();
	return err;
}

static int nl80211_leave_ibss(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *drv;
	struct net_device *dev;
	int err;

	rtnl_lock();

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		goto unlock_rtnl;

	if (!drv->ops->leave_ibss) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_ADHOC) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (!netif_running(dev)) {
		err = -ENETDOWN;
		goto out;
	}

	err = cfg80211_leave_ibss(drv, dev, false);

out:
	cfg80211_put_dev(drv);
	dev_put(dev);
unlock_rtnl:
	rtnl_unlock();
	return err;
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
		.cmd = NL80211_CMD_GET_REG,
		.doit = nl80211_get_reg,
		.policy = nl80211_policy,
		/* can be retrieved by unprivileged users */
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
	{
		.cmd = NL80211_CMD_TRIGGER_SCAN,
		.doit = nl80211_trigger_scan,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_GET_SCAN,
		.policy = nl80211_policy,
		.dumpit = nl80211_dump_scan,
	},
	{
		.cmd = NL80211_CMD_AUTHENTICATE,
		.doit = nl80211_authenticate,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_ASSOCIATE,
		.doit = nl80211_associate,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_DEAUTHENTICATE,
		.doit = nl80211_deauthenticate,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_DISASSOCIATE,
		.doit = nl80211_disassociate,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_JOIN_IBSS,
		.doit = nl80211_join_ibss,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_LEAVE_IBSS,
		.doit = nl80211_leave_ibss,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
};
static struct genl_multicast_group nl80211_mlme_mcgrp = {
	.name = "mlme",
};

/* multicast groups */
static struct genl_multicast_group nl80211_config_mcgrp = {
	.name = "config",
};
static struct genl_multicast_group nl80211_scan_mcgrp = {
	.name = "scan",
};
static struct genl_multicast_group nl80211_regulatory_mcgrp = {
	.name = "regulatory",
};

/* notification functions */

void nl80211_notify_dev_rename(struct cfg80211_registered_device *rdev)
{
	struct sk_buff *msg;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	if (nl80211_send_wiphy(msg, 0, 0, 0, rdev) < 0) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast(msg, 0, nl80211_config_mcgrp.id, GFP_KERNEL);
}

static int nl80211_add_scan_req(struct sk_buff *msg,
				struct cfg80211_registered_device *rdev)
{
	struct cfg80211_scan_request *req = rdev->scan_req;
	struct nlattr *nest;
	int i;

	if (WARN_ON(!req))
		return 0;

	nest = nla_nest_start(msg, NL80211_ATTR_SCAN_SSIDS);
	if (!nest)
		goto nla_put_failure;
	for (i = 0; i < req->n_ssids; i++)
		NLA_PUT(msg, i, req->ssids[i].ssid_len, req->ssids[i].ssid);
	nla_nest_end(msg, nest);

	nest = nla_nest_start(msg, NL80211_ATTR_SCAN_FREQUENCIES);
	if (!nest)
		goto nla_put_failure;
	for (i = 0; i < req->n_channels; i++)
		NLA_PUT_U32(msg, i, req->channels[i]->center_freq);
	nla_nest_end(msg, nest);

	if (req->ie)
		NLA_PUT(msg, NL80211_ATTR_IE, req->ie_len, req->ie);

	return 0;
 nla_put_failure:
	return -ENOBUFS;
}

static int nl80211_send_scan_donemsg(struct sk_buff *msg,
				     struct cfg80211_registered_device *rdev,
				     struct net_device *netdev,
				     u32 pid, u32 seq, int flags,
				     u32 cmd)
{
	void *hdr;

	hdr = nl80211hdr_put(msg, pid, seq, flags, cmd);
	if (!hdr)
		return -1;

	NLA_PUT_U32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx);
	NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, netdev->ifindex);

	/* ignore errors and send incomplete event anyway */
	nl80211_add_scan_req(msg, rdev);

	return genlmsg_end(msg, hdr);

 nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

void nl80211_send_scan_done(struct cfg80211_registered_device *rdev,
			    struct net_device *netdev)
{
	struct sk_buff *msg;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	if (nl80211_send_scan_donemsg(msg, rdev, netdev, 0, 0, 0,
				      NL80211_CMD_NEW_SCAN_RESULTS) < 0) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast(msg, 0, nl80211_scan_mcgrp.id, GFP_KERNEL);
}

void nl80211_send_scan_aborted(struct cfg80211_registered_device *rdev,
			       struct net_device *netdev)
{
	struct sk_buff *msg;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	if (nl80211_send_scan_donemsg(msg, rdev, netdev, 0, 0, 0,
				      NL80211_CMD_SCAN_ABORTED) < 0) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast(msg, 0, nl80211_scan_mcgrp.id, GFP_KERNEL);
}

/*
 * This can happen on global regulatory changes or device specific settings
 * based on custom world regulatory domains.
 */
void nl80211_send_reg_change_event(struct regulatory_request *request)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_REG_CHANGE);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	/* Userspace can always count this one always being set */
	NLA_PUT_U8(msg, NL80211_ATTR_REG_INITIATOR, request->initiator);

	if (request->alpha2[0] == '0' && request->alpha2[1] == '0')
		NLA_PUT_U8(msg, NL80211_ATTR_REG_TYPE,
			   NL80211_REGDOM_TYPE_WORLD);
	else if (request->alpha2[0] == '9' && request->alpha2[1] == '9')
		NLA_PUT_U8(msg, NL80211_ATTR_REG_TYPE,
			   NL80211_REGDOM_TYPE_CUSTOM_WORLD);
	else if ((request->alpha2[0] == '9' && request->alpha2[1] == '8') ||
		 request->intersect)
		NLA_PUT_U8(msg, NL80211_ATTR_REG_TYPE,
			   NL80211_REGDOM_TYPE_INTERSECTION);
	else {
		NLA_PUT_U8(msg, NL80211_ATTR_REG_TYPE,
			   NL80211_REGDOM_TYPE_COUNTRY);
		NLA_PUT_STRING(msg, NL80211_ATTR_REG_ALPHA2, request->alpha2);
	}

	if (wiphy_idx_valid(request->wiphy_idx))
		NLA_PUT_U32(msg, NL80211_ATTR_WIPHY, request->wiphy_idx);

	if (genlmsg_end(msg, hdr) < 0) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast(msg, 0, nl80211_regulatory_mcgrp.id, GFP_KERNEL);

	return;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	nlmsg_free(msg);
}

static void nl80211_send_mlme_event(struct cfg80211_registered_device *rdev,
				    struct net_device *netdev,
				    const u8 *buf, size_t len,
				    enum nl80211_commands cmd)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, cmd);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	NLA_PUT_U32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx);
	NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, netdev->ifindex);
	NLA_PUT(msg, NL80211_ATTR_FRAME, len, buf);

	if (genlmsg_end(msg, hdr) < 0) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast(msg, 0, nl80211_mlme_mcgrp.id, GFP_ATOMIC);
	return;

 nla_put_failure:
	genlmsg_cancel(msg, hdr);
	nlmsg_free(msg);
}

void nl80211_send_rx_auth(struct cfg80211_registered_device *rdev,
			  struct net_device *netdev, const u8 *buf, size_t len)
{
	nl80211_send_mlme_event(rdev, netdev, buf, len,
				NL80211_CMD_AUTHENTICATE);
}

void nl80211_send_rx_assoc(struct cfg80211_registered_device *rdev,
			   struct net_device *netdev, const u8 *buf,
			   size_t len)
{
	nl80211_send_mlme_event(rdev, netdev, buf, len, NL80211_CMD_ASSOCIATE);
}

void nl80211_send_deauth(struct cfg80211_registered_device *rdev,
			 struct net_device *netdev, const u8 *buf, size_t len)
{
	nl80211_send_mlme_event(rdev, netdev, buf, len,
				NL80211_CMD_DEAUTHENTICATE);
}

void nl80211_send_disassoc(struct cfg80211_registered_device *rdev,
			   struct net_device *netdev, const u8 *buf,
			   size_t len)
{
	nl80211_send_mlme_event(rdev, netdev, buf, len,
				NL80211_CMD_DISASSOCIATE);
}

static void nl80211_send_mlme_timeout(struct cfg80211_registered_device *rdev,
				      struct net_device *netdev, int cmd,
				      const u8 *addr)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, cmd);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	NLA_PUT_U32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx);
	NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, netdev->ifindex);
	NLA_PUT_FLAG(msg, NL80211_ATTR_TIMED_OUT);
	NLA_PUT(msg, NL80211_ATTR_MAC, ETH_ALEN, addr);

	if (genlmsg_end(msg, hdr) < 0) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast(msg, 0, nl80211_mlme_mcgrp.id, GFP_ATOMIC);
	return;

 nla_put_failure:
	genlmsg_cancel(msg, hdr);
	nlmsg_free(msg);
}

void nl80211_send_auth_timeout(struct cfg80211_registered_device *rdev,
			       struct net_device *netdev, const u8 *addr)
{
	nl80211_send_mlme_timeout(rdev, netdev, NL80211_CMD_AUTHENTICATE,
				  addr);
}

void nl80211_send_assoc_timeout(struct cfg80211_registered_device *rdev,
				struct net_device *netdev, const u8 *addr)
{
	nl80211_send_mlme_timeout(rdev, netdev, NL80211_CMD_ASSOCIATE, addr);
}

void nl80211_send_ibss_bssid(struct cfg80211_registered_device *rdev,
			     struct net_device *netdev, const u8 *bssid,
			     gfp_t gfp)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, gfp);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_JOIN_IBSS);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	NLA_PUT_U32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx);
	NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, netdev->ifindex);
	NLA_PUT(msg, NL80211_ATTR_MAC, ETH_ALEN, bssid);

	if (genlmsg_end(msg, hdr) < 0) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast(msg, 0, nl80211_mlme_mcgrp.id, gfp);
	return;

 nla_put_failure:
	genlmsg_cancel(msg, hdr);
	nlmsg_free(msg);
}

void nl80211_michael_mic_failure(struct cfg80211_registered_device *rdev,
				 struct net_device *netdev, const u8 *addr,
				 enum nl80211_key_type key_type, int key_id,
				 const u8 *tsc)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_MICHAEL_MIC_FAILURE);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	NLA_PUT_U32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx);
	NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, netdev->ifindex);
	if (addr)
		NLA_PUT(msg, NL80211_ATTR_MAC, ETH_ALEN, addr);
	NLA_PUT_U32(msg, NL80211_ATTR_KEY_TYPE, key_type);
	NLA_PUT_U8(msg, NL80211_ATTR_KEY_IDX, key_id);
	if (tsc)
		NLA_PUT(msg, NL80211_ATTR_KEY_SEQ, 6, tsc);

	if (genlmsg_end(msg, hdr) < 0) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast(msg, 0, nl80211_mlme_mcgrp.id, GFP_ATOMIC);
	return;

 nla_put_failure:
	genlmsg_cancel(msg, hdr);
	nlmsg_free(msg);
}

void nl80211_send_beacon_hint_event(struct wiphy *wiphy,
				    struct ieee80211_channel *channel_before,
				    struct ieee80211_channel *channel_after)
{
	struct sk_buff *msg;
	void *hdr;
	struct nlattr *nl_freq;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_REG_BEACON_HINT);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	/*
	 * Since we are applying the beacon hint to a wiphy we know its
	 * wiphy_idx is valid
	 */
	NLA_PUT_U32(msg, NL80211_ATTR_WIPHY, get_wiphy_idx(wiphy));

	/* Before */
	nl_freq = nla_nest_start(msg, NL80211_ATTR_FREQ_BEFORE);
	if (!nl_freq)
		goto nla_put_failure;
	if (nl80211_msg_put_channel(msg, channel_before))
		goto nla_put_failure;
	nla_nest_end(msg, nl_freq);

	/* After */
	nl_freq = nla_nest_start(msg, NL80211_ATTR_FREQ_AFTER);
	if (!nl_freq)
		goto nla_put_failure;
	if (nl80211_msg_put_channel(msg, channel_after))
		goto nla_put_failure;
	nla_nest_end(msg, nl_freq);

	if (genlmsg_end(msg, hdr) < 0) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast(msg, 0, nl80211_regulatory_mcgrp.id, GFP_ATOMIC);

	return;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	nlmsg_free(msg);
}

/* initialisation/exit functions */

int nl80211_init(void)
{
	int err;

	err = genl_register_family_with_ops(&nl80211_fam,
		nl80211_ops, ARRAY_SIZE(nl80211_ops));
	if (err)
		return err;

	err = genl_register_mc_group(&nl80211_fam, &nl80211_config_mcgrp);
	if (err)
		goto err_out;

	err = genl_register_mc_group(&nl80211_fam, &nl80211_scan_mcgrp);
	if (err)
		goto err_out;

	err = genl_register_mc_group(&nl80211_fam, &nl80211_regulatory_mcgrp);
	if (err)
		goto err_out;

	err = genl_register_mc_group(&nl80211_fam, &nl80211_mlme_mcgrp);
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
