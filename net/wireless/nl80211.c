/*
 * This is the new netlink-based wireless configuration interface.
 *
 * Copyright 2006-2010	Johannes Berg <johannes@sipsolutions.net>
 */

#include <linux/if.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/if_ether.h>
#include <linux/ieee80211.h>
#include <linux/nl80211.h>
#include <linux/rtnetlink.h>
#include <linux/netlink.h>
#include <linux/etherdevice.h>
#include <net/net_namespace.h>
#include <net/genetlink.h>
#include <net/cfg80211.h>
#include <net/sock.h>
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
	.netnsok = true,
};

/* internal helper: get rdev and dev */
static int get_rdev_dev_by_info_ifindex(struct genl_info *info,
				       struct cfg80211_registered_device **rdev,
				       struct net_device **dev)
{
	struct nlattr **attrs = info->attrs;
	int ifindex;

	if (!attrs[NL80211_ATTR_IFINDEX])
		return -EINVAL;

	ifindex = nla_get_u32(attrs[NL80211_ATTR_IFINDEX]);
	*dev = dev_get_by_index(genl_info_net(info), ifindex);
	if (!*dev)
		return -ENODEV;

	*rdev = cfg80211_get_dev_from_ifindex(genl_info_net(info), ifindex);
	if (IS_ERR(*rdev)) {
		dev_put(*dev);
		return PTR_ERR(*rdev);
	}

	return 0;
}

/* policy for the attributes */
static const struct nla_policy nl80211_policy[NL80211_ATTR_MAX+1] = {
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
	[NL80211_ATTR_WIPHY_COVERAGE_CLASS] = { .type = NLA_U8 },

	[NL80211_ATTR_IFTYPE] = { .type = NLA_U32 },
	[NL80211_ATTR_IFINDEX] = { .type = NLA_U32 },
	[NL80211_ATTR_IFNAME] = { .type = NLA_NUL_STRING, .len = IFNAMSIZ-1 },

	[NL80211_ATTR_MAC] = { .type = NLA_BINARY, .len = ETH_ALEN },
	[NL80211_ATTR_PREV_BSSID] = { .type = NLA_BINARY, .len = ETH_ALEN },

	[NL80211_ATTR_KEY] = { .type = NLA_NESTED, },
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
	[NL80211_ATTR_CONTROL_PORT_ETHERTYPE] = { .type = NLA_U16 },
	[NL80211_ATTR_CONTROL_PORT_NO_ENCRYPT] = { .type = NLA_FLAG },
	[NL80211_ATTR_PRIVACY] = { .type = NLA_FLAG },
	[NL80211_ATTR_CIPHER_SUITE_GROUP] = { .type = NLA_U32 },
	[NL80211_ATTR_WPA_VERSIONS] = { .type = NLA_U32 },
	[NL80211_ATTR_PID] = { .type = NLA_U32 },
	[NL80211_ATTR_4ADDR] = { .type = NLA_U8 },
	[NL80211_ATTR_PMKID] = { .type = NLA_BINARY,
				 .len = WLAN_PMKID_LEN },
	[NL80211_ATTR_DURATION] = { .type = NLA_U32 },
	[NL80211_ATTR_COOKIE] = { .type = NLA_U64 },
	[NL80211_ATTR_TX_RATES] = { .type = NLA_NESTED },
	[NL80211_ATTR_FRAME] = { .type = NLA_BINARY,
				 .len = IEEE80211_MAX_DATA_LEN },
	[NL80211_ATTR_FRAME_MATCH] = { .type = NLA_BINARY, },
	[NL80211_ATTR_PS_STATE] = { .type = NLA_U32 },
	[NL80211_ATTR_CQM] = { .type = NLA_NESTED, },
	[NL80211_ATTR_LOCAL_STATE_CHANGE] = { .type = NLA_FLAG },
	[NL80211_ATTR_AP_ISOLATE] = { .type = NLA_U8 },

	[NL80211_ATTR_WIPHY_TX_POWER_SETTING] = { .type = NLA_U32 },
	[NL80211_ATTR_WIPHY_TX_POWER_LEVEL] = { .type = NLA_U32 },
	[NL80211_ATTR_FRAME_TYPE] = { .type = NLA_U16 },
};

/* policy for the attributes */
static const struct nla_policy nl80211_key_policy[NL80211_KEY_MAX + 1] = {
	[NL80211_KEY_DATA] = { .type = NLA_BINARY, .len = WLAN_MAX_KEY_LEN },
	[NL80211_KEY_IDX] = { .type = NLA_U8 },
	[NL80211_KEY_CIPHER] = { .type = NLA_U32 },
	[NL80211_KEY_SEQ] = { .type = NLA_BINARY, .len = 8 },
	[NL80211_KEY_DEFAULT] = { .type = NLA_FLAG },
	[NL80211_KEY_DEFAULT_MGMT] = { .type = NLA_FLAG },
};

/* ifidx get helper */
static int nl80211_get_ifidx(struct netlink_callback *cb)
{
	int res;

	res = nlmsg_parse(cb->nlh, GENL_HDRLEN + nl80211_fam.hdrsize,
			  nl80211_fam.attrbuf, nl80211_fam.maxattr,
			  nl80211_policy);
	if (res)
		return res;

	if (!nl80211_fam.attrbuf[NL80211_ATTR_IFINDEX])
		return -EINVAL;

	res = nla_get_u32(nl80211_fam.attrbuf[NL80211_ATTR_IFINDEX]);
	if (!res)
		return -EINVAL;
	return res;
}

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

struct key_parse {
	struct key_params p;
	int idx;
	bool def, defmgmt;
};

static int nl80211_parse_key_new(struct nlattr *key, struct key_parse *k)
{
	struct nlattr *tb[NL80211_KEY_MAX + 1];
	int err = nla_parse_nested(tb, NL80211_KEY_MAX, key,
				   nl80211_key_policy);
	if (err)
		return err;

	k->def = !!tb[NL80211_KEY_DEFAULT];
	k->defmgmt = !!tb[NL80211_KEY_DEFAULT_MGMT];

	if (tb[NL80211_KEY_IDX])
		k->idx = nla_get_u8(tb[NL80211_KEY_IDX]);

	if (tb[NL80211_KEY_DATA]) {
		k->p.key = nla_data(tb[NL80211_KEY_DATA]);
		k->p.key_len = nla_len(tb[NL80211_KEY_DATA]);
	}

	if (tb[NL80211_KEY_SEQ]) {
		k->p.seq = nla_data(tb[NL80211_KEY_SEQ]);
		k->p.seq_len = nla_len(tb[NL80211_KEY_SEQ]);
	}

	if (tb[NL80211_KEY_CIPHER])
		k->p.cipher = nla_get_u32(tb[NL80211_KEY_CIPHER]);

	return 0;
}

static int nl80211_parse_key_old(struct genl_info *info, struct key_parse *k)
{
	if (info->attrs[NL80211_ATTR_KEY_DATA]) {
		k->p.key = nla_data(info->attrs[NL80211_ATTR_KEY_DATA]);
		k->p.key_len = nla_len(info->attrs[NL80211_ATTR_KEY_DATA]);
	}

	if (info->attrs[NL80211_ATTR_KEY_SEQ]) {
		k->p.seq = nla_data(info->attrs[NL80211_ATTR_KEY_SEQ]);
		k->p.seq_len = nla_len(info->attrs[NL80211_ATTR_KEY_SEQ]);
	}

	if (info->attrs[NL80211_ATTR_KEY_IDX])
		k->idx = nla_get_u8(info->attrs[NL80211_ATTR_KEY_IDX]);

	if (info->attrs[NL80211_ATTR_KEY_CIPHER])
		k->p.cipher = nla_get_u32(info->attrs[NL80211_ATTR_KEY_CIPHER]);

	k->def = !!info->attrs[NL80211_ATTR_KEY_DEFAULT];
	k->defmgmt = !!info->attrs[NL80211_ATTR_KEY_DEFAULT_MGMT];

	return 0;
}

static int nl80211_parse_key(struct genl_info *info, struct key_parse *k)
{
	int err;

	memset(k, 0, sizeof(*k));
	k->idx = -1;

	if (info->attrs[NL80211_ATTR_KEY])
		err = nl80211_parse_key_new(info->attrs[NL80211_ATTR_KEY], k);
	else
		err = nl80211_parse_key_old(info, k);

	if (err)
		return err;

	if (k->def && k->defmgmt)
		return -EINVAL;

	if (k->idx != -1) {
		if (k->defmgmt) {
			if (k->idx < 4 || k->idx > 5)
				return -EINVAL;
		} else if (k->def) {
			if (k->idx < 0 || k->idx > 3)
				return -EINVAL;
		} else {
			if (k->idx < 0 || k->idx > 5)
				return -EINVAL;
		}
	}

	return 0;
}

static struct cfg80211_cached_keys *
nl80211_parse_connkeys(struct cfg80211_registered_device *rdev,
		       struct nlattr *keys)
{
	struct key_parse parse;
	struct nlattr *key;
	struct cfg80211_cached_keys *result;
	int rem, err, def = 0;

	result = kzalloc(sizeof(*result), GFP_KERNEL);
	if (!result)
		return ERR_PTR(-ENOMEM);

	result->def = -1;
	result->defmgmt = -1;

	nla_for_each_nested(key, keys, rem) {
		memset(&parse, 0, sizeof(parse));
		parse.idx = -1;

		err = nl80211_parse_key_new(key, &parse);
		if (err)
			goto error;
		err = -EINVAL;
		if (!parse.p.key)
			goto error;
		if (parse.idx < 0 || parse.idx > 4)
			goto error;
		if (parse.def) {
			if (def)
				goto error;
			def = 1;
			result->def = parse.idx;
		} else if (parse.defmgmt)
			goto error;
		err = cfg80211_validate_key_settings(rdev, &parse.p,
						     parse.idx, NULL);
		if (err)
			goto error;
		result->params[parse.idx].cipher = parse.p.cipher;
		result->params[parse.idx].key_len = parse.p.key_len;
		result->params[parse.idx].key = result->data[parse.idx];
		memcpy(result->data[parse.idx], parse.p.key, parse.p.key_len);
	}

	return result;
 error:
	kfree(result);
	return ERR_PTR(err);
}

static int nl80211_key_allowed(struct wireless_dev *wdev)
{
	ASSERT_WDEV_LOCK(wdev);

	if (!netif_running(wdev->netdev))
		return -ENETDOWN;

	switch (wdev->iftype) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_P2P_GO:
		break;
	case NL80211_IFTYPE_ADHOC:
		if (!wdev->current_bss)
			return -ENOLINK;
		break;
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		if (wdev->sme_state != CFG80211_SME_CONNECTED)
			return -ENOLINK;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

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
	const struct ieee80211_txrx_stypes *mgmt_stypes =
				dev->wiphy.mgmt_stypes;

	hdr = nl80211hdr_put(msg, pid, seq, flags, NL80211_CMD_NEW_WIPHY);
	if (!hdr)
		return -1;

	NLA_PUT_U32(msg, NL80211_ATTR_WIPHY, dev->wiphy_idx);
	NLA_PUT_STRING(msg, NL80211_ATTR_WIPHY_NAME, wiphy_name(&dev->wiphy));

	NLA_PUT_U32(msg, NL80211_ATTR_GENERATION,
		    cfg80211_rdev_list_generation);

	NLA_PUT_U8(msg, NL80211_ATTR_WIPHY_RETRY_SHORT,
		   dev->wiphy.retry_short);
	NLA_PUT_U8(msg, NL80211_ATTR_WIPHY_RETRY_LONG,
		   dev->wiphy.retry_long);
	NLA_PUT_U32(msg, NL80211_ATTR_WIPHY_FRAG_THRESHOLD,
		    dev->wiphy.frag_threshold);
	NLA_PUT_U32(msg, NL80211_ATTR_WIPHY_RTS_THRESHOLD,
		    dev->wiphy.rts_threshold);
	NLA_PUT_U8(msg, NL80211_ATTR_WIPHY_COVERAGE_CLASS,
		    dev->wiphy.coverage_class);

	NLA_PUT_U8(msg, NL80211_ATTR_MAX_NUM_SCAN_SSIDS,
		   dev->wiphy.max_scan_ssids);
	NLA_PUT_U16(msg, NL80211_ATTR_MAX_SCAN_IE_LEN,
		    dev->wiphy.max_scan_ie_len);

	NLA_PUT(msg, NL80211_ATTR_CIPHER_SUITES,
		sizeof(u32) * dev->wiphy.n_cipher_suites,
		dev->wiphy.cipher_suites);

	NLA_PUT_U8(msg, NL80211_ATTR_MAX_NUM_PMKIDS,
		   dev->wiphy.max_num_pmkids);

	if (dev->wiphy.flags & WIPHY_FLAG_CONTROL_PORT_PROTOCOL)
		NLA_PUT_FLAG(msg, NL80211_ATTR_CONTROL_PORT_ETHERTYPE);

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
	CMD(set_pmksa, SET_PMKSA);
	CMD(del_pmksa, DEL_PMKSA);
	CMD(flush_pmksa, FLUSH_PMKSA);
	CMD(remain_on_channel, REMAIN_ON_CHANNEL);
	CMD(set_bitrate_mask, SET_TX_BITRATE_MASK);
	CMD(mgmt_tx, FRAME);
	if (dev->wiphy.flags & WIPHY_FLAG_NETNS_OK) {
		i++;
		NLA_PUT_U32(msg, i, NL80211_CMD_SET_WIPHY_NETNS);
	}
	CMD(set_channel, SET_CHANNEL);

#undef CMD

	if (dev->ops->connect || dev->ops->auth) {
		i++;
		NLA_PUT_U32(msg, i, NL80211_CMD_CONNECT);
	}

	if (dev->ops->disconnect || dev->ops->deauth) {
		i++;
		NLA_PUT_U32(msg, i, NL80211_CMD_DISCONNECT);
	}

	nla_nest_end(msg, nl_cmds);

	if (mgmt_stypes) {
		u16 stypes;
		struct nlattr *nl_ftypes, *nl_ifs;
		enum nl80211_iftype ift;

		nl_ifs = nla_nest_start(msg, NL80211_ATTR_TX_FRAME_TYPES);
		if (!nl_ifs)
			goto nla_put_failure;

		for (ift = 0; ift < NUM_NL80211_IFTYPES; ift++) {
			nl_ftypes = nla_nest_start(msg, ift);
			if (!nl_ftypes)
				goto nla_put_failure;
			i = 0;
			stypes = mgmt_stypes[ift].tx;
			while (stypes) {
				if (stypes & 1)
					NLA_PUT_U16(msg, NL80211_ATTR_FRAME_TYPE,
						    (i << 4) | IEEE80211_FTYPE_MGMT);
				stypes >>= 1;
				i++;
			}
			nla_nest_end(msg, nl_ftypes);
		}

		nla_nest_end(msg, nl_ifs);

		nl_ifs = nla_nest_start(msg, NL80211_ATTR_RX_FRAME_TYPES);
		if (!nl_ifs)
			goto nla_put_failure;

		for (ift = 0; ift < NUM_NL80211_IFTYPES; ift++) {
			nl_ftypes = nla_nest_start(msg, ift);
			if (!nl_ftypes)
				goto nla_put_failure;
			i = 0;
			stypes = mgmt_stypes[ift].rx;
			while (stypes) {
				if (stypes & 1)
					NLA_PUT_U16(msg, NL80211_ATTR_FRAME_TYPE,
						    (i << 4) | IEEE80211_FTYPE_MGMT);
				stypes >>= 1;
				i++;
			}
			nla_nest_end(msg, nl_ftypes);
		}
		nla_nest_end(msg, nl_ifs);
	}

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
	list_for_each_entry(dev, &cfg80211_rdev_list, list) {
		if (!net_eq(wiphy_net(&dev->wiphy), sock_net(skb->sk)))
			continue;
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

	cfg80211_unlock_rdev(dev);

	return genlmsg_reply(msg, info);

 out_free:
	nlmsg_free(msg);
 out_err:
	cfg80211_unlock_rdev(dev);
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

static bool nl80211_can_set_dev_channel(struct wireless_dev *wdev)
{
	/*
	 * You can only set the channel explicitly for AP, mesh
	 * and WDS type interfaces; all others have their channel
	 * managed via their respective "establish a connection"
	 * command (connect, join, ...)
	 *
	 * Monitors are special as they are normally slaved to
	 * whatever else is going on, so they behave as though
	 * you tried setting the wiphy channel itself.
	 */
	return !wdev ||
		wdev->iftype == NL80211_IFTYPE_AP ||
		wdev->iftype == NL80211_IFTYPE_WDS ||
		wdev->iftype == NL80211_IFTYPE_MESH_POINT ||
		wdev->iftype == NL80211_IFTYPE_MONITOR ||
		wdev->iftype == NL80211_IFTYPE_P2P_GO;
}

static int __nl80211_set_channel(struct cfg80211_registered_device *rdev,
				 struct wireless_dev *wdev,
				 struct genl_info *info)
{
	enum nl80211_channel_type channel_type = NL80211_CHAN_NO_HT;
	u32 freq;
	int result;

	if (!info->attrs[NL80211_ATTR_WIPHY_FREQ])
		return -EINVAL;

	if (!nl80211_can_set_dev_channel(wdev))
		return -EOPNOTSUPP;

	if (info->attrs[NL80211_ATTR_WIPHY_CHANNEL_TYPE]) {
		channel_type = nla_get_u32(info->attrs[
				   NL80211_ATTR_WIPHY_CHANNEL_TYPE]);
		if (channel_type != NL80211_CHAN_NO_HT &&
		    channel_type != NL80211_CHAN_HT20 &&
		    channel_type != NL80211_CHAN_HT40PLUS &&
		    channel_type != NL80211_CHAN_HT40MINUS)
			return -EINVAL;
	}

	freq = nla_get_u32(info->attrs[NL80211_ATTR_WIPHY_FREQ]);

	mutex_lock(&rdev->devlist_mtx);
	if (wdev) {
		wdev_lock(wdev);
		result = cfg80211_set_freq(rdev, wdev, freq, channel_type);
		wdev_unlock(wdev);
	} else {
		result = cfg80211_set_freq(rdev, NULL, freq, channel_type);
	}
	mutex_unlock(&rdev->devlist_mtx);

	return result;
}

static int nl80211_set_channel(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
	struct net_device *netdev;
	int result;

	rtnl_lock();

	result = get_rdev_dev_by_info_ifindex(info, &rdev, &netdev);
	if (result)
		goto unlock;

	result = __nl80211_set_channel(rdev, netdev->ieee80211_ptr, info);

 unlock:
	rtnl_unlock();

	return result;
}

static int nl80211_set_wiphy(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
	struct net_device *netdev = NULL;
	struct wireless_dev *wdev;
	int result = 0, rem_txq_params = 0;
	struct nlattr *nl_txq_params;
	u32 changed;
	u8 retry_short = 0, retry_long = 0;
	u32 frag_threshold = 0, rts_threshold = 0;
	u8 coverage_class = 0;

	rtnl_lock();

	/*
	 * Try to find the wiphy and netdev. Normally this
	 * function shouldn't need the netdev, but this is
	 * done for backward compatibility -- previously
	 * setting the channel was done per wiphy, but now
	 * it is per netdev. Previous userland like hostapd
	 * also passed a netdev to set_wiphy, so that it is
	 * possible to let that go to the right netdev!
	 */
	mutex_lock(&cfg80211_mutex);

	if (info->attrs[NL80211_ATTR_IFINDEX]) {
		int ifindex = nla_get_u32(info->attrs[NL80211_ATTR_IFINDEX]);

		netdev = dev_get_by_index(genl_info_net(info), ifindex);
		if (netdev && netdev->ieee80211_ptr) {
			rdev = wiphy_to_dev(netdev->ieee80211_ptr->wiphy);
			mutex_lock(&rdev->mtx);
		} else
			netdev = NULL;
	}

	if (!netdev) {
		rdev = __cfg80211_rdev_from_info(info);
		if (IS_ERR(rdev)) {
			mutex_unlock(&cfg80211_mutex);
			result = PTR_ERR(rdev);
			goto unlock;
		}
		wdev = NULL;
		netdev = NULL;
		result = 0;

		mutex_lock(&rdev->mtx);
	} else if (netif_running(netdev) &&
		   nl80211_can_set_dev_channel(netdev->ieee80211_ptr))
		wdev = netdev->ieee80211_ptr;
	else
		wdev = NULL;

	/*
	 * end workaround code, by now the rdev is available
	 * and locked, and wdev may or may not be NULL.
	 */

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
		result = __nl80211_set_channel(rdev, wdev, info);
		if (result)
			goto bad_res;
	}

	if (info->attrs[NL80211_ATTR_WIPHY_TX_POWER_SETTING]) {
		enum nl80211_tx_power_setting type;
		int idx, mbm = 0;

		if (!rdev->ops->set_tx_power) {
			result = -EOPNOTSUPP;
			goto bad_res;
		}

		idx = NL80211_ATTR_WIPHY_TX_POWER_SETTING;
		type = nla_get_u32(info->attrs[idx]);

		if (!info->attrs[NL80211_ATTR_WIPHY_TX_POWER_LEVEL] &&
		    (type != NL80211_TX_POWER_AUTOMATIC)) {
			result = -EINVAL;
			goto bad_res;
		}

		if (type != NL80211_TX_POWER_AUTOMATIC) {
			idx = NL80211_ATTR_WIPHY_TX_POWER_LEVEL;
			mbm = nla_get_u32(info->attrs[idx]);
		}

		result = rdev->ops->set_tx_power(&rdev->wiphy, type, mbm);
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

	if (info->attrs[NL80211_ATTR_WIPHY_COVERAGE_CLASS]) {
		coverage_class = nla_get_u8(
			info->attrs[NL80211_ATTR_WIPHY_COVERAGE_CLASS]);
		changed |= WIPHY_PARAM_COVERAGE_CLASS;
	}

	if (changed) {
		u8 old_retry_short, old_retry_long;
		u32 old_frag_threshold, old_rts_threshold;
		u8 old_coverage_class;

		if (!rdev->ops->set_wiphy_params) {
			result = -EOPNOTSUPP;
			goto bad_res;
		}

		old_retry_short = rdev->wiphy.retry_short;
		old_retry_long = rdev->wiphy.retry_long;
		old_frag_threshold = rdev->wiphy.frag_threshold;
		old_rts_threshold = rdev->wiphy.rts_threshold;
		old_coverage_class = rdev->wiphy.coverage_class;

		if (changed & WIPHY_PARAM_RETRY_SHORT)
			rdev->wiphy.retry_short = retry_short;
		if (changed & WIPHY_PARAM_RETRY_LONG)
			rdev->wiphy.retry_long = retry_long;
		if (changed & WIPHY_PARAM_FRAG_THRESHOLD)
			rdev->wiphy.frag_threshold = frag_threshold;
		if (changed & WIPHY_PARAM_RTS_THRESHOLD)
			rdev->wiphy.rts_threshold = rts_threshold;
		if (changed & WIPHY_PARAM_COVERAGE_CLASS)
			rdev->wiphy.coverage_class = coverage_class;

		result = rdev->ops->set_wiphy_params(&rdev->wiphy, changed);
		if (result) {
			rdev->wiphy.retry_short = old_retry_short;
			rdev->wiphy.retry_long = old_retry_long;
			rdev->wiphy.frag_threshold = old_frag_threshold;
			rdev->wiphy.rts_threshold = old_rts_threshold;
			rdev->wiphy.coverage_class = old_coverage_class;
		}
	}

 bad_res:
	mutex_unlock(&rdev->mtx);
	if (netdev)
		dev_put(netdev);
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

	NLA_PUT_U32(msg, NL80211_ATTR_GENERATION,
		    rdev->devlist_generation ^
			(cfg80211_rdev_list_generation << 2));

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
	struct cfg80211_registered_device *rdev;
	struct wireless_dev *wdev;

	mutex_lock(&cfg80211_mutex);
	list_for_each_entry(rdev, &cfg80211_rdev_list, list) {
		if (!net_eq(wiphy_net(&rdev->wiphy), sock_net(skb->sk)))
			continue;
		if (wp_idx < wp_start) {
			wp_idx++;
			continue;
		}
		if_idx = 0;

		mutex_lock(&rdev->devlist_mtx);
		list_for_each_entry(wdev, &rdev->netdev_list, list) {
			if (if_idx < if_start) {
				if_idx++;
				continue;
			}
			if (nl80211_send_iface(skb, NETLINK_CB(cb->skb).pid,
					       cb->nlh->nlmsg_seq, NLM_F_MULTI,
					       rdev, wdev->netdev) < 0) {
				mutex_unlock(&rdev->devlist_mtx);
				goto out;
			}
			if_idx++;
		}
		mutex_unlock(&rdev->devlist_mtx);

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

	err = get_rdev_dev_by_info_ifindex(info, &dev, &netdev);
	if (err)
		return err;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		goto out_err;

	if (nl80211_send_iface(msg, info->snd_pid, info->snd_seq, 0,
			       dev, netdev) < 0)
		goto out_free;

	dev_put(netdev);
	cfg80211_unlock_rdev(dev);

	return genlmsg_reply(msg, info);

 out_free:
	nlmsg_free(msg);
 out_err:
	dev_put(netdev);
	cfg80211_unlock_rdev(dev);
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

static int nl80211_valid_4addr(struct cfg80211_registered_device *rdev,
			       struct net_device *netdev, u8 use_4addr,
			       enum nl80211_iftype iftype)
{
	if (!use_4addr) {
		if (netdev && (netdev->priv_flags & IFF_BRIDGE_PORT))
			return -EBUSY;
		return 0;
	}

	switch (iftype) {
	case NL80211_IFTYPE_AP_VLAN:
		if (rdev->wiphy.flags & WIPHY_FLAG_4ADDR_AP)
			return 0;
		break;
	case NL80211_IFTYPE_STATION:
		if (rdev->wiphy.flags & WIPHY_FLAG_4ADDR_STATION)
			return 0;
		break;
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static int nl80211_set_interface(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
	struct vif_params params;
	int err;
	enum nl80211_iftype otype, ntype;
	struct net_device *dev;
	u32 _flags, *flags = NULL;
	bool change = false;

	memset(&params, 0, sizeof(params));

	rtnl_lock();

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto unlock_rtnl;

	otype = ntype = dev->ieee80211_ptr->iftype;

	if (info->attrs[NL80211_ATTR_IFTYPE]) {
		ntype = nla_get_u32(info->attrs[NL80211_ATTR_IFTYPE]);
		if (otype != ntype)
			change = true;
		if (ntype > NL80211_IFTYPE_MAX) {
			err = -EINVAL;
			goto unlock;
		}
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

	if (info->attrs[NL80211_ATTR_4ADDR]) {
		params.use_4addr = !!nla_get_u8(info->attrs[NL80211_ATTR_4ADDR]);
		change = true;
		err = nl80211_valid_4addr(rdev, dev, params.use_4addr, ntype);
		if (err)
			goto unlock;
	} else {
		params.use_4addr = -1;
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
		err = cfg80211_change_iface(rdev, dev, ntype, flags, &params);
	else
		err = 0;

	if (!err && params.use_4addr != -1)
		dev->ieee80211_ptr->use_4addr = params.use_4addr;

 unlock:
	dev_put(dev);
	cfg80211_unlock_rdev(rdev);
 unlock_rtnl:
	rtnl_unlock();
	return err;
}

static int nl80211_new_interface(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
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

	rdev = cfg80211_get_dev_from_info(info);
	if (IS_ERR(rdev)) {
		err = PTR_ERR(rdev);
		goto unlock_rtnl;
	}

	if (!rdev->ops->add_virtual_intf ||
	    !(rdev->wiphy.interface_modes & (1 << type))) {
		err = -EOPNOTSUPP;
		goto unlock;
	}

	if (type == NL80211_IFTYPE_MESH_POINT &&
	    info->attrs[NL80211_ATTR_MESH_ID]) {
		params.mesh_id = nla_data(info->attrs[NL80211_ATTR_MESH_ID]);
		params.mesh_id_len = nla_len(info->attrs[NL80211_ATTR_MESH_ID]);
	}

	if (info->attrs[NL80211_ATTR_4ADDR]) {
		params.use_4addr = !!nla_get_u8(info->attrs[NL80211_ATTR_4ADDR]);
		err = nl80211_valid_4addr(rdev, NULL, params.use_4addr, type);
		if (err)
			goto unlock;
	}

	err = parse_monitor_flags(type == NL80211_IFTYPE_MONITOR ?
				  info->attrs[NL80211_ATTR_MNTR_FLAGS] : NULL,
				  &flags);
	err = rdev->ops->add_virtual_intf(&rdev->wiphy,
		nla_data(info->attrs[NL80211_ATTR_IFNAME]),
		type, err ? NULL : &flags, &params);

 unlock:
	cfg80211_unlock_rdev(rdev);
 unlock_rtnl:
	rtnl_unlock();
	return err;
}

static int nl80211_del_interface(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
	int err;
	struct net_device *dev;

	rtnl_lock();

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto unlock_rtnl;

	if (!rdev->ops->del_virtual_intf) {
		err = -EOPNOTSUPP;
		goto out;
	}

	err = rdev->ops->del_virtual_intf(&rdev->wiphy, dev);

 out:
	cfg80211_unlock_rdev(rdev);
	dev_put(dev);
 unlock_rtnl:
	rtnl_unlock();
	return err;
}

struct get_key_cookie {
	struct sk_buff *msg;
	int error;
	int idx;
};

static void get_key_callback(void *c, struct key_params *params)
{
	struct nlattr *key;
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

	key = nla_nest_start(cookie->msg, NL80211_ATTR_KEY);
	if (!key)
		goto nla_put_failure;

	if (params->key)
		NLA_PUT(cookie->msg, NL80211_KEY_DATA,
			params->key_len, params->key);

	if (params->seq)
		NLA_PUT(cookie->msg, NL80211_KEY_SEQ,
			params->seq_len, params->seq);

	if (params->cipher)
		NLA_PUT_U32(cookie->msg, NL80211_KEY_CIPHER,
			    params->cipher);

	NLA_PUT_U8(cookie->msg, NL80211_ATTR_KEY_IDX, cookie->idx);

	nla_nest_end(cookie->msg, key);

	return;
 nla_put_failure:
	cookie->error = 1;
}

static int nl80211_get_key(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
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

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto unlock_rtnl;

	if (!rdev->ops->get_key) {
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
	cookie.idx = key_idx;

	NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, dev->ifindex);
	NLA_PUT_U8(msg, NL80211_ATTR_KEY_IDX, key_idx);
	if (mac_addr)
		NLA_PUT(msg, NL80211_ATTR_MAC, ETH_ALEN, mac_addr);

	err = rdev->ops->get_key(&rdev->wiphy, dev, key_idx, mac_addr,
				&cookie, get_key_callback);

	if (err)
		goto free_msg;

	if (cookie.error)
		goto nla_put_failure;

	genlmsg_end(msg, hdr);
	err = genlmsg_reply(msg, info);
	goto out;

 nla_put_failure:
	err = -ENOBUFS;
 free_msg:
	nlmsg_free(msg);
 out:
	cfg80211_unlock_rdev(rdev);
	dev_put(dev);
 unlock_rtnl:
	rtnl_unlock();

	return err;
}

static int nl80211_set_key(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
	struct key_parse key;
	int err;
	struct net_device *dev;
	int (*func)(struct wiphy *wiphy, struct net_device *netdev,
		    u8 key_index);

	err = nl80211_parse_key(info, &key);
	if (err)
		return err;

	if (key.idx < 0)
		return -EINVAL;

	/* only support setting default key */
	if (!key.def && !key.defmgmt)
		return -EINVAL;

	rtnl_lock();

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto unlock_rtnl;

	if (key.def)
		func = rdev->ops->set_default_key;
	else
		func = rdev->ops->set_default_mgmt_key;

	if (!func) {
		err = -EOPNOTSUPP;
		goto out;
	}

	wdev_lock(dev->ieee80211_ptr);
	err = nl80211_key_allowed(dev->ieee80211_ptr);
	if (!err)
		err = func(&rdev->wiphy, dev, key.idx);

#ifdef CONFIG_CFG80211_WEXT
	if (!err) {
		if (func == rdev->ops->set_default_key)
			dev->ieee80211_ptr->wext.default_key = key.idx;
		else
			dev->ieee80211_ptr->wext.default_mgmt_key = key.idx;
	}
#endif
	wdev_unlock(dev->ieee80211_ptr);

 out:
	cfg80211_unlock_rdev(rdev);
	dev_put(dev);

 unlock_rtnl:
	rtnl_unlock();

	return err;
}

static int nl80211_new_key(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
	int err;
	struct net_device *dev;
	struct key_parse key;
	u8 *mac_addr = NULL;

	err = nl80211_parse_key(info, &key);
	if (err)
		return err;

	if (!key.p.key)
		return -EINVAL;

	if (info->attrs[NL80211_ATTR_MAC])
		mac_addr = nla_data(info->attrs[NL80211_ATTR_MAC]);

	rtnl_lock();

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto unlock_rtnl;

	if (!rdev->ops->add_key) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (cfg80211_validate_key_settings(rdev, &key.p, key.idx, mac_addr)) {
		err = -EINVAL;
		goto out;
	}

	wdev_lock(dev->ieee80211_ptr);
	err = nl80211_key_allowed(dev->ieee80211_ptr);
	if (!err)
		err = rdev->ops->add_key(&rdev->wiphy, dev, key.idx,
					 mac_addr, &key.p);
	wdev_unlock(dev->ieee80211_ptr);

 out:
	cfg80211_unlock_rdev(rdev);
	dev_put(dev);
 unlock_rtnl:
	rtnl_unlock();

	return err;
}

static int nl80211_del_key(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
	int err;
	struct net_device *dev;
	u8 *mac_addr = NULL;
	struct key_parse key;

	err = nl80211_parse_key(info, &key);
	if (err)
		return err;

	if (info->attrs[NL80211_ATTR_MAC])
		mac_addr = nla_data(info->attrs[NL80211_ATTR_MAC]);

	rtnl_lock();

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto unlock_rtnl;

	if (!rdev->ops->del_key) {
		err = -EOPNOTSUPP;
		goto out;
	}

	wdev_lock(dev->ieee80211_ptr);
	err = nl80211_key_allowed(dev->ieee80211_ptr);
	if (!err)
		err = rdev->ops->del_key(&rdev->wiphy, dev, key.idx, mac_addr);

#ifdef CONFIG_CFG80211_WEXT
	if (!err) {
		if (key.idx == dev->ieee80211_ptr->wext.default_key)
			dev->ieee80211_ptr->wext.default_key = -1;
		else if (key.idx == dev->ieee80211_ptr->wext.default_mgmt_key)
			dev->ieee80211_ptr->wext.default_mgmt_key = -1;
	}
#endif
	wdev_unlock(dev->ieee80211_ptr);

 out:
	cfg80211_unlock_rdev(rdev);
	dev_put(dev);

 unlock_rtnl:
	rtnl_unlock();

	return err;
}

static int nl80211_addset_beacon(struct sk_buff *skb, struct genl_info *info)
{
        int (*call)(struct wiphy *wiphy, struct net_device *dev,
		    struct beacon_parameters *info);
	struct cfg80211_registered_device *rdev;
	int err;
	struct net_device *dev;
	struct beacon_parameters params;
	int haveinfo = 0;

	if (!is_valid_ie_attr(info->attrs[NL80211_ATTR_BEACON_TAIL]))
		return -EINVAL;

	rtnl_lock();

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto unlock_rtnl;

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_AP &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_GO) {
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

		call = rdev->ops->add_beacon;
		break;
	case NL80211_CMD_SET_BEACON:
		call = rdev->ops->set_beacon;
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

	err = call(&rdev->wiphy, dev, &params);

 out:
	cfg80211_unlock_rdev(rdev);
	dev_put(dev);
 unlock_rtnl:
	rtnl_unlock();

	return err;
}

static int nl80211_del_beacon(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
	int err;
	struct net_device *dev;

	rtnl_lock();

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto unlock_rtnl;

	if (!rdev->ops->del_beacon) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_AP &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_GO) {
		err = -EOPNOTSUPP;
		goto out;
	}
	err = rdev->ops->del_beacon(&rdev->wiphy, dev);

 out:
	cfg80211_unlock_rdev(rdev);
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

static int nl80211_send_station(struct sk_buff *msg, u32 pid, u32 seq,
				int flags, struct net_device *dev,
				const u8 *mac_addr, struct station_info *sinfo)
{
	void *hdr;
	struct nlattr *sinfoattr, *txrate;
	u16 bitrate;

	hdr = nl80211hdr_put(msg, pid, seq, flags, NL80211_CMD_NEW_STATION);
	if (!hdr)
		return -1;

	NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, dev->ifindex);
	NLA_PUT(msg, NL80211_ATTR_MAC, ETH_ALEN, mac_addr);

	NLA_PUT_U32(msg, NL80211_ATTR_GENERATION, sinfo->generation);

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

		/* cfg80211_calculate_bitrate will return 0 for mcs >= 32 */
		bitrate = cfg80211_calculate_bitrate(&sinfo->txrate);
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

	if (!ifidx)
		ifidx = nl80211_get_ifidx(cb);
	if (ifidx < 0)
		return ifidx;

	rtnl_lock();

	netdev = __dev_get_by_index(sock_net(skb->sk), ifidx);
	if (!netdev) {
		err = -ENODEV;
		goto out_rtnl;
	}

	dev = cfg80211_get_dev_from_ifindex(sock_net(skb->sk), ifidx);
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
	cfg80211_unlock_rdev(dev);
 out_rtnl:
	rtnl_unlock();

	return err;
}

static int nl80211_get_station(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
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

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto out_rtnl;

	if (!rdev->ops->get_station) {
		err = -EOPNOTSUPP;
		goto out;
	}

	err = rdev->ops->get_station(&rdev->wiphy, dev, mac_addr, &sinfo);
	if (err)
		goto out;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		goto out;

	if (nl80211_send_station(msg, info->snd_pid, info->snd_seq, 0,
				 dev, mac_addr, &sinfo) < 0)
		goto out_free;

	err = genlmsg_reply(msg, info);
	goto out;

 out_free:
	nlmsg_free(msg);
 out:
	cfg80211_unlock_rdev(rdev);
	dev_put(dev);
 out_rtnl:
	rtnl_unlock();

	return err;
}

/*
 * Get vlan interface making sure it is running and on the right wiphy.
 */
static int get_vlan(struct genl_info *info,
		    struct cfg80211_registered_device *rdev,
		    struct net_device **vlan)
{
	struct nlattr *vlanattr = info->attrs[NL80211_ATTR_STA_VLAN];
	*vlan = NULL;

	if (vlanattr) {
		*vlan = dev_get_by_index(genl_info_net(info),
					 nla_get_u32(vlanattr));
		if (!*vlan)
			return -ENODEV;
		if (!(*vlan)->ieee80211_ptr)
			return -EINVAL;
		if ((*vlan)->ieee80211_ptr->wiphy != &rdev->wiphy)
			return -EINVAL;
		if (!netif_running(*vlan))
			return -ENETDOWN;
	}
	return 0;
}

static int nl80211_set_station(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
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

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto out_rtnl;

	err = get_vlan(info, rdev, &params.vlan);
	if (err)
		goto out;

	/* validate settings */
	err = 0;

	switch (dev->ieee80211_ptr->iftype) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_P2P_GO:
		/* disallow mesh-specific things */
		if (params.plink_action)
			err = -EINVAL;
		break;
	case NL80211_IFTYPE_P2P_CLIENT:
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

	if (!rdev->ops->change_station) {
		err = -EOPNOTSUPP;
		goto out;
	}

	err = rdev->ops->change_station(&rdev->wiphy, dev, mac_addr, &params);

 out:
	if (params.vlan)
		dev_put(params.vlan);
	cfg80211_unlock_rdev(rdev);
	dev_put(dev);
 out_rtnl:
	rtnl_unlock();

	return err;
}

static int nl80211_new_station(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
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

	if (!info->attrs[NL80211_ATTR_STA_AID])
		return -EINVAL;

	mac_addr = nla_data(info->attrs[NL80211_ATTR_MAC]);
	params.supported_rates =
		nla_data(info->attrs[NL80211_ATTR_STA_SUPPORTED_RATES]);
	params.supported_rates_len =
		nla_len(info->attrs[NL80211_ATTR_STA_SUPPORTED_RATES]);
	params.listen_interval =
		nla_get_u16(info->attrs[NL80211_ATTR_STA_LISTEN_INTERVAL]);

	params.aid = nla_get_u16(info->attrs[NL80211_ATTR_STA_AID]);
	if (!params.aid || params.aid > IEEE80211_MAX_AID)
		return -EINVAL;

	if (info->attrs[NL80211_ATTR_HT_CAPABILITY])
		params.ht_capa =
			nla_data(info->attrs[NL80211_ATTR_HT_CAPABILITY]);

	if (parse_station_flags(info, &params))
		return -EINVAL;

	rtnl_lock();

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto out_rtnl;

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_AP &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_AP_VLAN &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_GO) {
		err = -EINVAL;
		goto out;
	}

	err = get_vlan(info, rdev, &params.vlan);
	if (err)
		goto out;

	/* validate settings */
	err = 0;

	if (!rdev->ops->add_station) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (!netif_running(dev)) {
		err = -ENETDOWN;
		goto out;
	}

	err = rdev->ops->add_station(&rdev->wiphy, dev, mac_addr, &params);

 out:
	if (params.vlan)
		dev_put(params.vlan);
	cfg80211_unlock_rdev(rdev);
	dev_put(dev);
 out_rtnl:
	rtnl_unlock();

	return err;
}

static int nl80211_del_station(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
	int err;
	struct net_device *dev;
	u8 *mac_addr = NULL;

	if (info->attrs[NL80211_ATTR_MAC])
		mac_addr = nla_data(info->attrs[NL80211_ATTR_MAC]);

	rtnl_lock();

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto out_rtnl;

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_AP &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_AP_VLAN &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_MESH_POINT &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_GO) {
		err = -EINVAL;
		goto out;
	}

	if (!rdev->ops->del_station) {
		err = -EOPNOTSUPP;
		goto out;
	}

	err = rdev->ops->del_station(&rdev->wiphy, dev, mac_addr);

 out:
	cfg80211_unlock_rdev(rdev);
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

	NLA_PUT_U32(msg, NL80211_ATTR_GENERATION, pinfo->generation);

	pinfoattr = nla_nest_start(msg, NL80211_ATTR_MPATH_INFO);
	if (!pinfoattr)
		goto nla_put_failure;
	if (pinfo->filled & MPATH_INFO_FRAME_QLEN)
		NLA_PUT_U32(msg, NL80211_MPATH_INFO_FRAME_QLEN,
			    pinfo->frame_qlen);
	if (pinfo->filled & MPATH_INFO_SN)
		NLA_PUT_U32(msg, NL80211_MPATH_INFO_SN,
			    pinfo->sn);
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

	if (!ifidx)
		ifidx = nl80211_get_ifidx(cb);
	if (ifidx < 0)
		return ifidx;

	rtnl_lock();

	netdev = __dev_get_by_index(sock_net(skb->sk), ifidx);
	if (!netdev) {
		err = -ENODEV;
		goto out_rtnl;
	}

	dev = cfg80211_get_dev_from_ifindex(sock_net(skb->sk), ifidx);
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
		goto out_err;
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
	cfg80211_unlock_rdev(dev);
 out_rtnl:
	rtnl_unlock();

	return err;
}

static int nl80211_get_mpath(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
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

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto out_rtnl;

	if (!rdev->ops->get_mpath) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_MESH_POINT) {
		err = -EOPNOTSUPP;
		goto out;
	}

	err = rdev->ops->get_mpath(&rdev->wiphy, dev, dst, next_hop, &pinfo);
	if (err)
		goto out;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		goto out;

	if (nl80211_send_mpath(msg, info->snd_pid, info->snd_seq, 0,
				 dev, dst, next_hop, &pinfo) < 0)
		goto out_free;

	err = genlmsg_reply(msg, info);
	goto out;

 out_free:
	nlmsg_free(msg);
 out:
	cfg80211_unlock_rdev(rdev);
	dev_put(dev);
 out_rtnl:
	rtnl_unlock();

	return err;
}

static int nl80211_set_mpath(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
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

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto out_rtnl;

	if (!rdev->ops->change_mpath) {
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

	err = rdev->ops->change_mpath(&rdev->wiphy, dev, dst, next_hop);

 out:
	cfg80211_unlock_rdev(rdev);
	dev_put(dev);
 out_rtnl:
	rtnl_unlock();

	return err;
}
static int nl80211_new_mpath(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
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

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto out_rtnl;

	if (!rdev->ops->add_mpath) {
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

	err = rdev->ops->add_mpath(&rdev->wiphy, dev, dst, next_hop);

 out:
	cfg80211_unlock_rdev(rdev);
	dev_put(dev);
 out_rtnl:
	rtnl_unlock();

	return err;
}

static int nl80211_del_mpath(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
	int err;
	struct net_device *dev;
	u8 *dst = NULL;

	if (info->attrs[NL80211_ATTR_MAC])
		dst = nla_data(info->attrs[NL80211_ATTR_MAC]);

	rtnl_lock();

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto out_rtnl;

	if (!rdev->ops->del_mpath) {
		err = -EOPNOTSUPP;
		goto out;
	}

	err = rdev->ops->del_mpath(&rdev->wiphy, dev, dst);

 out:
	cfg80211_unlock_rdev(rdev);
	dev_put(dev);
 out_rtnl:
	rtnl_unlock();

	return err;
}

static int nl80211_set_bss(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
	int err;
	struct net_device *dev;
	struct bss_parameters params;

	memset(&params, 0, sizeof(params));
	/* default to not changing parameters */
	params.use_cts_prot = -1;
	params.use_short_preamble = -1;
	params.use_short_slot_time = -1;
	params.ap_isolate = -1;

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
	if (info->attrs[NL80211_ATTR_AP_ISOLATE])
		params.ap_isolate = !!nla_get_u8(info->attrs[NL80211_ATTR_AP_ISOLATE]);

	rtnl_lock();

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto out_rtnl;

	if (!rdev->ops->change_bss) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_AP &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_GO) {
		err = -EOPNOTSUPP;
		goto out;
	}

	err = rdev->ops->change_bss(&rdev->wiphy, dev, &params);

 out:
	cfg80211_unlock_rdev(rdev);
	dev_put(dev);
 out_rtnl:
	rtnl_unlock();

	return err;
}

static const struct nla_policy reg_rule_policy[NL80211_REG_RULE_ATTR_MAX + 1] = {
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

	r = regulatory_hint_user(data);

	return r;
}

static int nl80211_get_mesh_params(struct sk_buff *skb,
	struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
	struct mesh_config cur_params;
	int err;
	struct net_device *dev;
	void *hdr;
	struct nlattr *pinfoattr;
	struct sk_buff *msg;

	rtnl_lock();

	/* Look up our device */
	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto out_rtnl;

	if (!rdev->ops->get_mesh_params) {
		err = -EOPNOTSUPP;
		goto out;
	}

	/* Get the mesh params */
	err = rdev->ops->get_mesh_params(&rdev->wiphy, dev, &cur_params);
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
	NLA_PUT_U8(msg, NL80211_MESHCONF_HWMP_ROOTMODE,
			cur_params.dot11MeshHWMPRootMode);
	nla_nest_end(msg, pinfoattr);
	genlmsg_end(msg, hdr);
	err = genlmsg_reply(msg, info);
	goto out;

 nla_put_failure:
	genlmsg_cancel(msg, hdr);
	nlmsg_free(msg);
	err = -EMSGSIZE;
 out:
	/* Cleanup */
	cfg80211_unlock_rdev(rdev);
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

static const struct nla_policy nl80211_meshconf_params_policy[NL80211_MESHCONF_ATTR_MAX+1] = {
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
	struct cfg80211_registered_device *rdev;
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

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto out_rtnl;

	if (!rdev->ops->set_mesh_params) {
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
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg,
			dot11MeshHWMPRootMode, mask,
			NL80211_MESHCONF_HWMP_ROOTMODE,
			nla_get_u8);

	/* Apply changes */
	err = rdev->ops->set_mesh_params(&rdev->wiphy, dev, &cfg, mask);

 out:
	/* cleanup */
	cfg80211_unlock_rdev(rdev);
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
	err = genlmsg_reply(msg, info);
	goto out;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	nlmsg_free(msg);
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

static int validate_scan_freqs(struct nlattr *freqs)
{
	struct nlattr *attr1, *attr2;
	int n_channels = 0, tmp1, tmp2;

	nla_for_each_nested(attr1, freqs, tmp1) {
		n_channels++;
		/*
		 * Some hardware has a limited channel list for
		 * scanning, and it is pretty much nonsensical
		 * to scan for a channel twice, so disallow that
		 * and don't require drivers to check that the
		 * channel list they get isn't longer than what
		 * they can scan, as long as they can scan all
		 * the channels they registered at once.
		 */
		nla_for_each_nested(attr2, freqs, tmp2)
			if (attr1 != attr2 &&
			    nla_get_u32(attr1) == nla_get_u32(attr2))
				return 0;
	}

	return n_channels;
}

static int nl80211_trigger_scan(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
	struct net_device *dev;
	struct cfg80211_scan_request *request;
	struct cfg80211_ssid *ssid;
	struct ieee80211_channel *channel;
	struct nlattr *attr;
	struct wiphy *wiphy;
	int err, tmp, n_ssids = 0, n_channels, i;
	enum ieee80211_band band;
	size_t ie_len;

	if (!is_valid_ie_attr(info->attrs[NL80211_ATTR_IE]))
		return -EINVAL;

	rtnl_lock();

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto out_rtnl;

	wiphy = &rdev->wiphy;

	if (!rdev->ops->scan) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (!netif_running(dev)) {
		err = -ENETDOWN;
		goto out;
	}

	if (rdev->scan_req) {
		err = -EBUSY;
		goto out;
	}

	if (info->attrs[NL80211_ATTR_SCAN_FREQUENCIES]) {
		n_channels = validate_scan_freqs(
				info->attrs[NL80211_ATTR_SCAN_FREQUENCIES]);
		if (!n_channels) {
			err = -EINVAL;
			goto out;
		}
	} else {
		n_channels = 0;

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

	if (n_ssids)
		request->ssids = (void *)&request->channels[n_channels];
	request->n_ssids = n_ssids;
	if (ie_len) {
		if (request->ssids)
			request->ie = (void *)(request->ssids + n_ssids);
		else
			request->ie = (void *)(request->channels + n_channels);
	}

	i = 0;
	if (info->attrs[NL80211_ATTR_SCAN_FREQUENCIES]) {
		/* user specified, bail out if channel not found */
		nla_for_each_nested(attr, info->attrs[NL80211_ATTR_SCAN_FREQUENCIES], tmp) {
			struct ieee80211_channel *chan;

			chan = ieee80211_get_channel(wiphy, nla_get_u32(attr));

			if (!chan) {
				err = -EINVAL;
				goto out_free;
			}

			/* ignore disabled channels */
			if (chan->flags & IEEE80211_CHAN_DISABLED)
				continue;

			request->channels[i] = chan;
			i++;
		}
	} else {
		/* all channels */
		for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
			int j;
			if (!wiphy->bands[band])
				continue;
			for (j = 0; j < wiphy->bands[band]->n_channels; j++) {
				struct ieee80211_channel *chan;

				chan = &wiphy->bands[band]->channels[j];

				if (chan->flags & IEEE80211_CHAN_DISABLED)
					continue;

				request->channels[i] = chan;
				i++;
			}
		}
	}

	if (!i) {
		err = -EINVAL;
		goto out_free;
	}

	request->n_channels = i;

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

	request->dev = dev;
	request->wiphy = &rdev->wiphy;

	rdev->scan_req = request;
	err = rdev->ops->scan(&rdev->wiphy, dev, request);

	if (!err) {
		nl80211_send_scan_start(rdev, dev);
		dev_hold(dev);
	}

 out_free:
	if (err) {
		rdev->scan_req = NULL;
		kfree(request);
	}
 out:
	cfg80211_unlock_rdev(rdev);
	dev_put(dev);
 out_rtnl:
	rtnl_unlock();

	return err;
}

static int nl80211_send_bss(struct sk_buff *msg, u32 pid, u32 seq, int flags,
			    struct cfg80211_registered_device *rdev,
			    struct wireless_dev *wdev,
			    struct cfg80211_internal_bss *intbss)
{
	struct cfg80211_bss *res = &intbss->pub;
	void *hdr;
	struct nlattr *bss;
	int i;

	ASSERT_WDEV_LOCK(wdev);

	hdr = nl80211hdr_put(msg, pid, seq, flags,
			     NL80211_CMD_NEW_SCAN_RESULTS);
	if (!hdr)
		return -1;

	NLA_PUT_U32(msg, NL80211_ATTR_GENERATION, rdev->bss_generation);
	NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, wdev->netdev->ifindex);

	bss = nla_nest_start(msg, NL80211_ATTR_BSS);
	if (!bss)
		goto nla_put_failure;
	if (!is_zero_ether_addr(res->bssid))
		NLA_PUT(msg, NL80211_BSS_BSSID, ETH_ALEN, res->bssid);
	if (res->information_elements && res->len_information_elements)
		NLA_PUT(msg, NL80211_BSS_INFORMATION_ELEMENTS,
			res->len_information_elements,
			res->information_elements);
	if (res->beacon_ies && res->len_beacon_ies &&
	    res->beacon_ies != res->information_elements)
		NLA_PUT(msg, NL80211_BSS_BEACON_IES,
			res->len_beacon_ies, res->beacon_ies);
	if (res->tsf)
		NLA_PUT_U64(msg, NL80211_BSS_TSF, res->tsf);
	if (res->beacon_interval)
		NLA_PUT_U16(msg, NL80211_BSS_BEACON_INTERVAL, res->beacon_interval);
	NLA_PUT_U16(msg, NL80211_BSS_CAPABILITY, res->capability);
	NLA_PUT_U32(msg, NL80211_BSS_FREQUENCY, res->channel->center_freq);
	NLA_PUT_U32(msg, NL80211_BSS_SEEN_MS_AGO,
		jiffies_to_msecs(jiffies - intbss->ts));

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

	switch (wdev->iftype) {
	case NL80211_IFTYPE_P2P_CLIENT:
	case NL80211_IFTYPE_STATION:
		if (intbss == wdev->current_bss)
			NLA_PUT_U32(msg, NL80211_BSS_STATUS,
				    NL80211_BSS_STATUS_ASSOCIATED);
		else for (i = 0; i < MAX_AUTH_BSSES; i++) {
			if (intbss != wdev->auth_bsses[i])
				continue;
			NLA_PUT_U32(msg, NL80211_BSS_STATUS,
				    NL80211_BSS_STATUS_AUTHENTICATED);
			break;
		}
		break;
	case NL80211_IFTYPE_ADHOC:
		if (intbss == wdev->current_bss)
			NLA_PUT_U32(msg, NL80211_BSS_STATUS,
				    NL80211_BSS_STATUS_IBSS_JOINED);
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
	struct cfg80211_registered_device *rdev;
	struct net_device *dev;
	struct cfg80211_internal_bss *scan;
	struct wireless_dev *wdev;
	int ifidx = cb->args[0];
	int start = cb->args[1], idx = 0;
	int err;

	if (!ifidx)
		ifidx = nl80211_get_ifidx(cb);
	if (ifidx < 0)
		return ifidx;
	cb->args[0] = ifidx;

	dev = dev_get_by_index(sock_net(skb->sk), ifidx);
	if (!dev)
		return -ENODEV;

	rdev = cfg80211_get_dev_from_ifindex(sock_net(skb->sk), ifidx);
	if (IS_ERR(rdev)) {
		err = PTR_ERR(rdev);
		goto out_put_netdev;
	}

	wdev = dev->ieee80211_ptr;

	wdev_lock(wdev);
	spin_lock_bh(&rdev->bss_lock);
	cfg80211_bss_expire(rdev);

	list_for_each_entry(scan, &rdev->bss_list, list) {
		if (++idx <= start)
			continue;
		if (nl80211_send_bss(skb,
				NETLINK_CB(cb->skb).pid,
				cb->nlh->nlmsg_seq, NLM_F_MULTI,
				rdev, wdev, scan) < 0) {
			idx--;
			goto out;
		}
	}

 out:
	spin_unlock_bh(&rdev->bss_lock);
	wdev_unlock(wdev);

	cb->args[1] = idx;
	err = skb->len;
	cfg80211_unlock_rdev(rdev);
 out_put_netdev:
	dev_put(dev);

	return err;
}

static int nl80211_send_survey(struct sk_buff *msg, u32 pid, u32 seq,
				int flags, struct net_device *dev,
				struct survey_info *survey)
{
	void *hdr;
	struct nlattr *infoattr;

	/* Survey without a channel doesn't make sense */
	if (!survey->channel)
		return -EINVAL;

	hdr = nl80211hdr_put(msg, pid, seq, flags,
			     NL80211_CMD_NEW_SURVEY_RESULTS);
	if (!hdr)
		return -ENOMEM;

	NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, dev->ifindex);

	infoattr = nla_nest_start(msg, NL80211_ATTR_SURVEY_INFO);
	if (!infoattr)
		goto nla_put_failure;

	NLA_PUT_U32(msg, NL80211_SURVEY_INFO_FREQUENCY,
		    survey->channel->center_freq);
	if (survey->filled & SURVEY_INFO_NOISE_DBM)
		NLA_PUT_U8(msg, NL80211_SURVEY_INFO_NOISE,
			    survey->noise);

	nla_nest_end(msg, infoattr);

	return genlmsg_end(msg, hdr);

 nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int nl80211_dump_survey(struct sk_buff *skb,
			struct netlink_callback *cb)
{
	struct survey_info survey;
	struct cfg80211_registered_device *dev;
	struct net_device *netdev;
	int ifidx = cb->args[0];
	int survey_idx = cb->args[1];
	int res;

	if (!ifidx)
		ifidx = nl80211_get_ifidx(cb);
	if (ifidx < 0)
		return ifidx;
	cb->args[0] = ifidx;

	rtnl_lock();

	netdev = __dev_get_by_index(sock_net(skb->sk), ifidx);
	if (!netdev) {
		res = -ENODEV;
		goto out_rtnl;
	}

	dev = cfg80211_get_dev_from_ifindex(sock_net(skb->sk), ifidx);
	if (IS_ERR(dev)) {
		res = PTR_ERR(dev);
		goto out_rtnl;
	}

	if (!dev->ops->dump_survey) {
		res = -EOPNOTSUPP;
		goto out_err;
	}

	while (1) {
		res = dev->ops->dump_survey(&dev->wiphy, netdev, survey_idx,
					    &survey);
		if (res == -ENOENT)
			break;
		if (res)
			goto out_err;

		if (nl80211_send_survey(skb,
				NETLINK_CB(cb->skb).pid,
				cb->nlh->nlmsg_seq, NLM_F_MULTI,
				netdev,
				&survey) < 0)
			goto out;
		survey_idx++;
	}

 out:
	cb->args[1] = survey_idx;
	res = skb->len;
 out_err:
	cfg80211_unlock_rdev(dev);
 out_rtnl:
	rtnl_unlock();

	return res;
}

static bool nl80211_valid_auth_type(enum nl80211_auth_type auth_type)
{
	return auth_type <= NL80211_AUTHTYPE_MAX;
}

static bool nl80211_valid_wpa_versions(u32 wpa_versions)
{
	return !(wpa_versions & ~(NL80211_WPA_VERSION_1 |
				  NL80211_WPA_VERSION_2));
}

static bool nl80211_valid_akm_suite(u32 akm)
{
	return akm == WLAN_AKM_SUITE_8021X ||
		akm == WLAN_AKM_SUITE_PSK;
}

static bool nl80211_valid_cipher_suite(u32 cipher)
{
	return cipher == WLAN_CIPHER_SUITE_WEP40 ||
		cipher == WLAN_CIPHER_SUITE_WEP104 ||
		cipher == WLAN_CIPHER_SUITE_TKIP ||
		cipher == WLAN_CIPHER_SUITE_CCMP ||
		cipher == WLAN_CIPHER_SUITE_AES_CMAC;
}


static int nl80211_authenticate(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
	struct net_device *dev;
	struct ieee80211_channel *chan;
	const u8 *bssid, *ssid, *ie = NULL;
	int err, ssid_len, ie_len = 0;
	enum nl80211_auth_type auth_type;
	struct key_parse key;
	bool local_state_change;

	if (!is_valid_ie_attr(info->attrs[NL80211_ATTR_IE]))
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_MAC])
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_AUTH_TYPE])
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_SSID])
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_WIPHY_FREQ])
		return -EINVAL;

	err = nl80211_parse_key(info, &key);
	if (err)
		return err;

	if (key.idx >= 0) {
		if (!key.p.key || !key.p.key_len)
			return -EINVAL;
		if ((key.p.cipher != WLAN_CIPHER_SUITE_WEP40 ||
		     key.p.key_len != WLAN_KEY_LEN_WEP40) &&
		    (key.p.cipher != WLAN_CIPHER_SUITE_WEP104 ||
		     key.p.key_len != WLAN_KEY_LEN_WEP104))
			return -EINVAL;
		if (key.idx > 4)
			return -EINVAL;
	} else {
		key.p.key_len = 0;
		key.p.key = NULL;
	}

	rtnl_lock();

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto unlock_rtnl;

	if (key.idx >= 0) {
		int i;
		bool ok = false;
		for (i = 0; i < rdev->wiphy.n_cipher_suites; i++) {
			if (key.p.cipher == rdev->wiphy.cipher_suites[i]) {
				ok = true;
				break;
			}
		}
		if (!ok) {
			err = -EINVAL;
			goto out;
		}
	}

	if (!rdev->ops->auth) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_STATION &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_CLIENT) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (!netif_running(dev)) {
		err = -ENETDOWN;
		goto out;
	}

	bssid = nla_data(info->attrs[NL80211_ATTR_MAC]);
	chan = ieee80211_get_channel(&rdev->wiphy,
		nla_get_u32(info->attrs[NL80211_ATTR_WIPHY_FREQ]));
	if (!chan || (chan->flags & IEEE80211_CHAN_DISABLED)) {
		err = -EINVAL;
		goto out;
	}

	ssid = nla_data(info->attrs[NL80211_ATTR_SSID]);
	ssid_len = nla_len(info->attrs[NL80211_ATTR_SSID]);

	if (info->attrs[NL80211_ATTR_IE]) {
		ie = nla_data(info->attrs[NL80211_ATTR_IE]);
		ie_len = nla_len(info->attrs[NL80211_ATTR_IE]);
	}

	auth_type = nla_get_u32(info->attrs[NL80211_ATTR_AUTH_TYPE]);
	if (!nl80211_valid_auth_type(auth_type)) {
		err = -EINVAL;
		goto out;
	}

	local_state_change = !!info->attrs[NL80211_ATTR_LOCAL_STATE_CHANGE];

	err = cfg80211_mlme_auth(rdev, dev, chan, auth_type, bssid,
				 ssid, ssid_len, ie, ie_len,
				 key.p.key, key.p.key_len, key.idx,
				 local_state_change);

out:
	cfg80211_unlock_rdev(rdev);
	dev_put(dev);
unlock_rtnl:
	rtnl_unlock();
	return err;
}

static int nl80211_crypto_settings(struct cfg80211_registered_device *rdev,
				   struct genl_info *info,
				   struct cfg80211_crypto_settings *settings,
				   int cipher_limit)
{
	memset(settings, 0, sizeof(*settings));

	settings->control_port = info->attrs[NL80211_ATTR_CONTROL_PORT];

	if (info->attrs[NL80211_ATTR_CONTROL_PORT_ETHERTYPE]) {
		u16 proto;
		proto = nla_get_u16(
			info->attrs[NL80211_ATTR_CONTROL_PORT_ETHERTYPE]);
		settings->control_port_ethertype = cpu_to_be16(proto);
		if (!(rdev->wiphy.flags & WIPHY_FLAG_CONTROL_PORT_PROTOCOL) &&
		    proto != ETH_P_PAE)
			return -EINVAL;
		if (info->attrs[NL80211_ATTR_CONTROL_PORT_NO_ENCRYPT])
			settings->control_port_no_encrypt = true;
	} else
		settings->control_port_ethertype = cpu_to_be16(ETH_P_PAE);

	if (info->attrs[NL80211_ATTR_CIPHER_SUITES_PAIRWISE]) {
		void *data;
		int len, i;

		data = nla_data(info->attrs[NL80211_ATTR_CIPHER_SUITES_PAIRWISE]);
		len = nla_len(info->attrs[NL80211_ATTR_CIPHER_SUITES_PAIRWISE]);
		settings->n_ciphers_pairwise = len / sizeof(u32);

		if (len % sizeof(u32))
			return -EINVAL;

		if (settings->n_ciphers_pairwise > cipher_limit)
			return -EINVAL;

		memcpy(settings->ciphers_pairwise, data, len);

		for (i = 0; i < settings->n_ciphers_pairwise; i++)
			if (!nl80211_valid_cipher_suite(
					settings->ciphers_pairwise[i]))
				return -EINVAL;
	}

	if (info->attrs[NL80211_ATTR_CIPHER_SUITE_GROUP]) {
		settings->cipher_group =
			nla_get_u32(info->attrs[NL80211_ATTR_CIPHER_SUITE_GROUP]);
		if (!nl80211_valid_cipher_suite(settings->cipher_group))
			return -EINVAL;
	}

	if (info->attrs[NL80211_ATTR_WPA_VERSIONS]) {
		settings->wpa_versions =
			nla_get_u32(info->attrs[NL80211_ATTR_WPA_VERSIONS]);
		if (!nl80211_valid_wpa_versions(settings->wpa_versions))
			return -EINVAL;
	}

	if (info->attrs[NL80211_ATTR_AKM_SUITES]) {
		void *data;
		int len, i;

		data = nla_data(info->attrs[NL80211_ATTR_AKM_SUITES]);
		len = nla_len(info->attrs[NL80211_ATTR_AKM_SUITES]);
		settings->n_akm_suites = len / sizeof(u32);

		if (len % sizeof(u32))
			return -EINVAL;

		memcpy(settings->akm_suites, data, len);

		for (i = 0; i < settings->n_ciphers_pairwise; i++)
			if (!nl80211_valid_akm_suite(settings->akm_suites[i]))
				return -EINVAL;
	}

	return 0;
}

static int nl80211_associate(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
	struct net_device *dev;
	struct cfg80211_crypto_settings crypto;
	struct ieee80211_channel *chan;
	const u8 *bssid, *ssid, *ie = NULL, *prev_bssid = NULL;
	int err, ssid_len, ie_len = 0;
	bool use_mfp = false;

	if (!is_valid_ie_attr(info->attrs[NL80211_ATTR_IE]))
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_MAC] ||
	    !info->attrs[NL80211_ATTR_SSID] ||
	    !info->attrs[NL80211_ATTR_WIPHY_FREQ])
		return -EINVAL;

	rtnl_lock();

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto unlock_rtnl;

	if (!rdev->ops->assoc) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_STATION &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_CLIENT) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (!netif_running(dev)) {
		err = -ENETDOWN;
		goto out;
	}

	bssid = nla_data(info->attrs[NL80211_ATTR_MAC]);

	chan = ieee80211_get_channel(&rdev->wiphy,
		nla_get_u32(info->attrs[NL80211_ATTR_WIPHY_FREQ]));
	if (!chan || (chan->flags & IEEE80211_CHAN_DISABLED)) {
		err = -EINVAL;
		goto out;
	}

	ssid = nla_data(info->attrs[NL80211_ATTR_SSID]);
	ssid_len = nla_len(info->attrs[NL80211_ATTR_SSID]);

	if (info->attrs[NL80211_ATTR_IE]) {
		ie = nla_data(info->attrs[NL80211_ATTR_IE]);
		ie_len = nla_len(info->attrs[NL80211_ATTR_IE]);
	}

	if (info->attrs[NL80211_ATTR_USE_MFP]) {
		enum nl80211_mfp mfp =
			nla_get_u32(info->attrs[NL80211_ATTR_USE_MFP]);
		if (mfp == NL80211_MFP_REQUIRED)
			use_mfp = true;
		else if (mfp != NL80211_MFP_NO) {
			err = -EINVAL;
			goto out;
		}
	}

	if (info->attrs[NL80211_ATTR_PREV_BSSID])
		prev_bssid = nla_data(info->attrs[NL80211_ATTR_PREV_BSSID]);

	err = nl80211_crypto_settings(rdev, info, &crypto, 1);
	if (!err)
		err = cfg80211_mlme_assoc(rdev, dev, chan, bssid, prev_bssid,
					  ssid, ssid_len, ie, ie_len, use_mfp,
					  &crypto);

out:
	cfg80211_unlock_rdev(rdev);
	dev_put(dev);
unlock_rtnl:
	rtnl_unlock();
	return err;
}

static int nl80211_deauthenticate(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
	struct net_device *dev;
	const u8 *ie = NULL, *bssid;
	int err, ie_len = 0;
	u16 reason_code;
	bool local_state_change;

	if (!is_valid_ie_attr(info->attrs[NL80211_ATTR_IE]))
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_MAC])
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_REASON_CODE])
		return -EINVAL;

	rtnl_lock();

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto unlock_rtnl;

	if (!rdev->ops->deauth) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_STATION &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_CLIENT) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (!netif_running(dev)) {
		err = -ENETDOWN;
		goto out;
	}

	bssid = nla_data(info->attrs[NL80211_ATTR_MAC]);

	reason_code = nla_get_u16(info->attrs[NL80211_ATTR_REASON_CODE]);
	if (reason_code == 0) {
		/* Reason Code 0 is reserved */
		err = -EINVAL;
		goto out;
	}

	if (info->attrs[NL80211_ATTR_IE]) {
		ie = nla_data(info->attrs[NL80211_ATTR_IE]);
		ie_len = nla_len(info->attrs[NL80211_ATTR_IE]);
	}

	local_state_change = !!info->attrs[NL80211_ATTR_LOCAL_STATE_CHANGE];

	err = cfg80211_mlme_deauth(rdev, dev, bssid, ie, ie_len, reason_code,
				   local_state_change);

out:
	cfg80211_unlock_rdev(rdev);
	dev_put(dev);
unlock_rtnl:
	rtnl_unlock();
	return err;
}

static int nl80211_disassociate(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
	struct net_device *dev;
	const u8 *ie = NULL, *bssid;
	int err, ie_len = 0;
	u16 reason_code;
	bool local_state_change;

	if (!is_valid_ie_attr(info->attrs[NL80211_ATTR_IE]))
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_MAC])
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_REASON_CODE])
		return -EINVAL;

	rtnl_lock();

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto unlock_rtnl;

	if (!rdev->ops->disassoc) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_STATION &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_CLIENT) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (!netif_running(dev)) {
		err = -ENETDOWN;
		goto out;
	}

	bssid = nla_data(info->attrs[NL80211_ATTR_MAC]);

	reason_code = nla_get_u16(info->attrs[NL80211_ATTR_REASON_CODE]);
	if (reason_code == 0) {
		/* Reason Code 0 is reserved */
		err = -EINVAL;
		goto out;
	}

	if (info->attrs[NL80211_ATTR_IE]) {
		ie = nla_data(info->attrs[NL80211_ATTR_IE]);
		ie_len = nla_len(info->attrs[NL80211_ATTR_IE]);
	}

	local_state_change = !!info->attrs[NL80211_ATTR_LOCAL_STATE_CHANGE];

	err = cfg80211_mlme_disassoc(rdev, dev, bssid, ie, ie_len, reason_code,
				     local_state_change);

out:
	cfg80211_unlock_rdev(rdev);
	dev_put(dev);
unlock_rtnl:
	rtnl_unlock();
	return err;
}

static int nl80211_join_ibss(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
	struct net_device *dev;
	struct cfg80211_ibss_params ibss;
	struct wiphy *wiphy;
	struct cfg80211_cached_keys *connkeys = NULL;
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

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto unlock_rtnl;

	if (!rdev->ops->join_ibss) {
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

	wiphy = &rdev->wiphy;

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
	ibss.privacy = !!info->attrs[NL80211_ATTR_PRIVACY];

	if (ibss.privacy && info->attrs[NL80211_ATTR_KEYS]) {
		connkeys = nl80211_parse_connkeys(rdev,
					info->attrs[NL80211_ATTR_KEYS]);
		if (IS_ERR(connkeys)) {
			err = PTR_ERR(connkeys);
			connkeys = NULL;
			goto out;
		}
	}

	if (info->attrs[NL80211_ATTR_BSS_BASIC_RATES]) {
		u8 *rates =
			nla_data(info->attrs[NL80211_ATTR_BSS_BASIC_RATES]);
		int n_rates =
			nla_len(info->attrs[NL80211_ATTR_BSS_BASIC_RATES]);
		struct ieee80211_supported_band *sband =
			wiphy->bands[ibss.channel->band];
		int i, j;

		if (n_rates == 0) {
			err = -EINVAL;
			goto out;
		}

		for (i = 0; i < n_rates; i++) {
			int rate = (rates[i] & 0x7f) * 5;
			bool found = false;

			for (j = 0; j < sband->n_bitrates; j++) {
				if (sband->bitrates[j].bitrate == rate) {
					found = true;
					ibss.basic_rates |= BIT(j);
					break;
				}
			}
			if (!found) {
				err = -EINVAL;
				goto out;
			}
		}
	}

	err = cfg80211_join_ibss(rdev, dev, &ibss, connkeys);

out:
	cfg80211_unlock_rdev(rdev);
	dev_put(dev);
unlock_rtnl:
	if (err)
		kfree(connkeys);
	rtnl_unlock();
	return err;
}

static int nl80211_leave_ibss(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
	struct net_device *dev;
	int err;

	rtnl_lock();

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto unlock_rtnl;

	if (!rdev->ops->leave_ibss) {
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

	err = cfg80211_leave_ibss(rdev, dev, false);

out:
	cfg80211_unlock_rdev(rdev);
	dev_put(dev);
unlock_rtnl:
	rtnl_unlock();
	return err;
}

#ifdef CONFIG_NL80211_TESTMODE
static struct genl_multicast_group nl80211_testmode_mcgrp = {
	.name = "testmode",
};

static int nl80211_testmode_do(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
	int err;

	if (!info->attrs[NL80211_ATTR_TESTDATA])
		return -EINVAL;

	rtnl_lock();

	rdev = cfg80211_get_dev_from_info(info);
	if (IS_ERR(rdev)) {
		err = PTR_ERR(rdev);
		goto unlock_rtnl;
	}

	err = -EOPNOTSUPP;
	if (rdev->ops->testmode_cmd) {
		rdev->testmode_info = info;
		err = rdev->ops->testmode_cmd(&rdev->wiphy,
				nla_data(info->attrs[NL80211_ATTR_TESTDATA]),
				nla_len(info->attrs[NL80211_ATTR_TESTDATA]));
		rdev->testmode_info = NULL;
	}

	cfg80211_unlock_rdev(rdev);

 unlock_rtnl:
	rtnl_unlock();
	return err;
}

static struct sk_buff *
__cfg80211_testmode_alloc_skb(struct cfg80211_registered_device *rdev,
			      int approxlen, u32 pid, u32 seq, gfp_t gfp)
{
	struct sk_buff *skb;
	void *hdr;
	struct nlattr *data;

	skb = nlmsg_new(approxlen + 100, gfp);
	if (!skb)
		return NULL;

	hdr = nl80211hdr_put(skb, pid, seq, 0, NL80211_CMD_TESTMODE);
	if (!hdr) {
		kfree_skb(skb);
		return NULL;
	}

	NLA_PUT_U32(skb, NL80211_ATTR_WIPHY, rdev->wiphy_idx);
	data = nla_nest_start(skb, NL80211_ATTR_TESTDATA);

	((void **)skb->cb)[0] = rdev;
	((void **)skb->cb)[1] = hdr;
	((void **)skb->cb)[2] = data;

	return skb;

 nla_put_failure:
	kfree_skb(skb);
	return NULL;
}

struct sk_buff *cfg80211_testmode_alloc_reply_skb(struct wiphy *wiphy,
						  int approxlen)
{
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);

	if (WARN_ON(!rdev->testmode_info))
		return NULL;

	return __cfg80211_testmode_alloc_skb(rdev, approxlen,
				rdev->testmode_info->snd_pid,
				rdev->testmode_info->snd_seq,
				GFP_KERNEL);
}
EXPORT_SYMBOL(cfg80211_testmode_alloc_reply_skb);

int cfg80211_testmode_reply(struct sk_buff *skb)
{
	struct cfg80211_registered_device *rdev = ((void **)skb->cb)[0];
	void *hdr = ((void **)skb->cb)[1];
	struct nlattr *data = ((void **)skb->cb)[2];

	if (WARN_ON(!rdev->testmode_info)) {
		kfree_skb(skb);
		return -EINVAL;
	}

	nla_nest_end(skb, data);
	genlmsg_end(skb, hdr);
	return genlmsg_reply(skb, rdev->testmode_info);
}
EXPORT_SYMBOL(cfg80211_testmode_reply);

struct sk_buff *cfg80211_testmode_alloc_event_skb(struct wiphy *wiphy,
						  int approxlen, gfp_t gfp)
{
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);

	return __cfg80211_testmode_alloc_skb(rdev, approxlen, 0, 0, gfp);
}
EXPORT_SYMBOL(cfg80211_testmode_alloc_event_skb);

void cfg80211_testmode_event(struct sk_buff *skb, gfp_t gfp)
{
	void *hdr = ((void **)skb->cb)[1];
	struct nlattr *data = ((void **)skb->cb)[2];

	nla_nest_end(skb, data);
	genlmsg_end(skb, hdr);
	genlmsg_multicast(skb, 0, nl80211_testmode_mcgrp.id, gfp);
}
EXPORT_SYMBOL(cfg80211_testmode_event);
#endif

static int nl80211_connect(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
	struct net_device *dev;
	struct cfg80211_connect_params connect;
	struct wiphy *wiphy;
	struct cfg80211_cached_keys *connkeys = NULL;
	int err;

	memset(&connect, 0, sizeof(connect));

	if (!is_valid_ie_attr(info->attrs[NL80211_ATTR_IE]))
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_SSID] ||
	    !nla_len(info->attrs[NL80211_ATTR_SSID]))
		return -EINVAL;

	if (info->attrs[NL80211_ATTR_AUTH_TYPE]) {
		connect.auth_type =
			nla_get_u32(info->attrs[NL80211_ATTR_AUTH_TYPE]);
		if (!nl80211_valid_auth_type(connect.auth_type))
			return -EINVAL;
	} else
		connect.auth_type = NL80211_AUTHTYPE_AUTOMATIC;

	connect.privacy = info->attrs[NL80211_ATTR_PRIVACY];

	err = nl80211_crypto_settings(rdev, info, &connect.crypto,
				      NL80211_MAX_NR_CIPHER_SUITES);
	if (err)
		return err;
	rtnl_lock();

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto unlock_rtnl;

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_STATION &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_CLIENT) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (!netif_running(dev)) {
		err = -ENETDOWN;
		goto out;
	}

	wiphy = &rdev->wiphy;

	if (info->attrs[NL80211_ATTR_MAC])
		connect.bssid = nla_data(info->attrs[NL80211_ATTR_MAC]);
	connect.ssid = nla_data(info->attrs[NL80211_ATTR_SSID]);
	connect.ssid_len = nla_len(info->attrs[NL80211_ATTR_SSID]);

	if (info->attrs[NL80211_ATTR_IE]) {
		connect.ie = nla_data(info->attrs[NL80211_ATTR_IE]);
		connect.ie_len = nla_len(info->attrs[NL80211_ATTR_IE]);
	}

	if (info->attrs[NL80211_ATTR_WIPHY_FREQ]) {
		connect.channel =
			ieee80211_get_channel(wiphy,
			    nla_get_u32(info->attrs[NL80211_ATTR_WIPHY_FREQ]));
		if (!connect.channel ||
		    connect.channel->flags & IEEE80211_CHAN_DISABLED) {
			err = -EINVAL;
			goto out;
		}
	}

	if (connect.privacy && info->attrs[NL80211_ATTR_KEYS]) {
		connkeys = nl80211_parse_connkeys(rdev,
					info->attrs[NL80211_ATTR_KEYS]);
		if (IS_ERR(connkeys)) {
			err = PTR_ERR(connkeys);
			connkeys = NULL;
			goto out;
		}
	}

	err = cfg80211_connect(rdev, dev, &connect, connkeys);

out:
	cfg80211_unlock_rdev(rdev);
	dev_put(dev);
unlock_rtnl:
	if (err)
		kfree(connkeys);
	rtnl_unlock();
	return err;
}

static int nl80211_disconnect(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
	struct net_device *dev;
	int err;
	u16 reason;

	if (!info->attrs[NL80211_ATTR_REASON_CODE])
		reason = WLAN_REASON_DEAUTH_LEAVING;
	else
		reason = nla_get_u16(info->attrs[NL80211_ATTR_REASON_CODE]);

	if (reason == 0)
		return -EINVAL;

	rtnl_lock();

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto unlock_rtnl;

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_STATION &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_CLIENT) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (!netif_running(dev)) {
		err = -ENETDOWN;
		goto out;
	}

	err = cfg80211_disconnect(rdev, dev, reason, true);

out:
	cfg80211_unlock_rdev(rdev);
	dev_put(dev);
unlock_rtnl:
	rtnl_unlock();
	return err;
}

static int nl80211_wiphy_netns(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
	struct net *net;
	int err;
	u32 pid;

	if (!info->attrs[NL80211_ATTR_PID])
		return -EINVAL;

	pid = nla_get_u32(info->attrs[NL80211_ATTR_PID]);

	rtnl_lock();

	rdev = cfg80211_get_dev_from_info(info);
	if (IS_ERR(rdev)) {
		err = PTR_ERR(rdev);
		goto out_rtnl;
	}

	net = get_net_ns_by_pid(pid);
	if (IS_ERR(net)) {
		err = PTR_ERR(net);
		goto out;
	}

	err = 0;

	/* check if anything to do */
	if (net_eq(wiphy_net(&rdev->wiphy), net))
		goto out_put_net;

	err = cfg80211_switch_netns(rdev, net);
 out_put_net:
	put_net(net);
 out:
	cfg80211_unlock_rdev(rdev);
 out_rtnl:
	rtnl_unlock();
	return err;
}

static int nl80211_setdel_pmksa(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
	int (*rdev_ops)(struct wiphy *wiphy, struct net_device *dev,
			struct cfg80211_pmksa *pmksa) = NULL;
	int err;
	struct net_device *dev;
	struct cfg80211_pmksa pmksa;

	memset(&pmksa, 0, sizeof(struct cfg80211_pmksa));

	if (!info->attrs[NL80211_ATTR_MAC])
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_PMKID])
		return -EINVAL;

	rtnl_lock();

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto out_rtnl;

	pmksa.pmkid = nla_data(info->attrs[NL80211_ATTR_PMKID]);
	pmksa.bssid = nla_data(info->attrs[NL80211_ATTR_MAC]);

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_STATION &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_CLIENT) {
		err = -EOPNOTSUPP;
		goto out;
	}

	switch (info->genlhdr->cmd) {
	case NL80211_CMD_SET_PMKSA:
		rdev_ops = rdev->ops->set_pmksa;
		break;
	case NL80211_CMD_DEL_PMKSA:
		rdev_ops = rdev->ops->del_pmksa;
		break;
	default:
		WARN_ON(1);
		break;
	}

	if (!rdev_ops) {
		err = -EOPNOTSUPP;
		goto out;
	}

	err = rdev_ops(&rdev->wiphy, dev, &pmksa);

 out:
	cfg80211_unlock_rdev(rdev);
	dev_put(dev);
 out_rtnl:
	rtnl_unlock();

	return err;
}

static int nl80211_flush_pmksa(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
	int err;
	struct net_device *dev;

	rtnl_lock();

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto out_rtnl;

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_STATION &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_CLIENT) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (!rdev->ops->flush_pmksa) {
		err = -EOPNOTSUPP;
		goto out;
	}

	err = rdev->ops->flush_pmksa(&rdev->wiphy, dev);

 out:
	cfg80211_unlock_rdev(rdev);
	dev_put(dev);
 out_rtnl:
	rtnl_unlock();

	return err;

}

static int nl80211_remain_on_channel(struct sk_buff *skb,
				     struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
	struct net_device *dev;
	struct ieee80211_channel *chan;
	struct sk_buff *msg;
	void *hdr;
	u64 cookie;
	enum nl80211_channel_type channel_type = NL80211_CHAN_NO_HT;
	u32 freq, duration;
	int err;

	if (!info->attrs[NL80211_ATTR_WIPHY_FREQ] ||
	    !info->attrs[NL80211_ATTR_DURATION])
		return -EINVAL;

	duration = nla_get_u32(info->attrs[NL80211_ATTR_DURATION]);

	/*
	 * We should be on that channel for at least one jiffie,
	 * and more than 5 seconds seems excessive.
	 */
	if (!duration || !msecs_to_jiffies(duration) || duration > 5000)
		return -EINVAL;

	rtnl_lock();

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto unlock_rtnl;

	if (!rdev->ops->remain_on_channel) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (!netif_running(dev)) {
		err = -ENETDOWN;
		goto out;
	}

	if (info->attrs[NL80211_ATTR_WIPHY_CHANNEL_TYPE]) {
		channel_type = nla_get_u32(
			info->attrs[NL80211_ATTR_WIPHY_CHANNEL_TYPE]);
		if (channel_type != NL80211_CHAN_NO_HT &&
		    channel_type != NL80211_CHAN_HT20 &&
		    channel_type != NL80211_CHAN_HT40PLUS &&
		    channel_type != NL80211_CHAN_HT40MINUS) {
			err = -EINVAL;
			goto out;
		}
	}

	freq = nla_get_u32(info->attrs[NL80211_ATTR_WIPHY_FREQ]);
	chan = rdev_freq_to_chan(rdev, freq, channel_type);
	if (chan == NULL) {
		err = -EINVAL;
		goto out;
	}

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		err = -ENOMEM;
		goto out;
	}

	hdr = nl80211hdr_put(msg, info->snd_pid, info->snd_seq, 0,
			     NL80211_CMD_REMAIN_ON_CHANNEL);

	if (IS_ERR(hdr)) {
		err = PTR_ERR(hdr);
		goto free_msg;
	}

	err = rdev->ops->remain_on_channel(&rdev->wiphy, dev, chan,
					   channel_type, duration, &cookie);

	if (err)
		goto free_msg;

	NLA_PUT_U64(msg, NL80211_ATTR_COOKIE, cookie);

	genlmsg_end(msg, hdr);
	err = genlmsg_reply(msg, info);
	goto out;

 nla_put_failure:
	err = -ENOBUFS;
 free_msg:
	nlmsg_free(msg);
 out:
	cfg80211_unlock_rdev(rdev);
	dev_put(dev);
 unlock_rtnl:
	rtnl_unlock();
	return err;
}

static int nl80211_cancel_remain_on_channel(struct sk_buff *skb,
					    struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
	struct net_device *dev;
	u64 cookie;
	int err;

	if (!info->attrs[NL80211_ATTR_COOKIE])
		return -EINVAL;

	rtnl_lock();

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto unlock_rtnl;

	if (!rdev->ops->cancel_remain_on_channel) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (!netif_running(dev)) {
		err = -ENETDOWN;
		goto out;
	}

	cookie = nla_get_u64(info->attrs[NL80211_ATTR_COOKIE]);

	err = rdev->ops->cancel_remain_on_channel(&rdev->wiphy, dev, cookie);

 out:
	cfg80211_unlock_rdev(rdev);
	dev_put(dev);
 unlock_rtnl:
	rtnl_unlock();
	return err;
}

static u32 rateset_to_mask(struct ieee80211_supported_band *sband,
			   u8 *rates, u8 rates_len)
{
	u8 i;
	u32 mask = 0;

	for (i = 0; i < rates_len; i++) {
		int rate = (rates[i] & 0x7f) * 5;
		int ridx;
		for (ridx = 0; ridx < sband->n_bitrates; ridx++) {
			struct ieee80211_rate *srate =
				&sband->bitrates[ridx];
			if (rate == srate->bitrate) {
				mask |= 1 << ridx;
				break;
			}
		}
		if (ridx == sband->n_bitrates)
			return 0; /* rate not found */
	}

	return mask;
}

static const struct nla_policy nl80211_txattr_policy[NL80211_TXRATE_MAX + 1] = {
	[NL80211_TXRATE_LEGACY] = { .type = NLA_BINARY,
				    .len = NL80211_MAX_SUPP_RATES },
};

static int nl80211_set_tx_bitrate_mask(struct sk_buff *skb,
				       struct genl_info *info)
{
	struct nlattr *tb[NL80211_TXRATE_MAX + 1];
	struct cfg80211_registered_device *rdev;
	struct cfg80211_bitrate_mask mask;
	int err, rem, i;
	struct net_device *dev;
	struct nlattr *tx_rates;
	struct ieee80211_supported_band *sband;

	if (info->attrs[NL80211_ATTR_TX_RATES] == NULL)
		return -EINVAL;

	rtnl_lock();

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto unlock_rtnl;

	if (!rdev->ops->set_bitrate_mask) {
		err = -EOPNOTSUPP;
		goto unlock;
	}

	memset(&mask, 0, sizeof(mask));
	/* Default to all rates enabled */
	for (i = 0; i < IEEE80211_NUM_BANDS; i++) {
		sband = rdev->wiphy.bands[i];
		mask.control[i].legacy =
			sband ? (1 << sband->n_bitrates) - 1 : 0;
	}

	/*
	 * The nested attribute uses enum nl80211_band as the index. This maps
	 * directly to the enum ieee80211_band values used in cfg80211.
	 */
	nla_for_each_nested(tx_rates, info->attrs[NL80211_ATTR_TX_RATES], rem)
	{
		enum ieee80211_band band = nla_type(tx_rates);
		if (band < 0 || band >= IEEE80211_NUM_BANDS) {
			err = -EINVAL;
			goto unlock;
		}
		sband = rdev->wiphy.bands[band];
		if (sband == NULL) {
			err = -EINVAL;
			goto unlock;
		}
		nla_parse(tb, NL80211_TXRATE_MAX, nla_data(tx_rates),
			  nla_len(tx_rates), nl80211_txattr_policy);
		if (tb[NL80211_TXRATE_LEGACY]) {
			mask.control[band].legacy = rateset_to_mask(
				sband,
				nla_data(tb[NL80211_TXRATE_LEGACY]),
				nla_len(tb[NL80211_TXRATE_LEGACY]));
			if (mask.control[band].legacy == 0) {
				err = -EINVAL;
				goto unlock;
			}
		}
	}

	err = rdev->ops->set_bitrate_mask(&rdev->wiphy, dev, NULL, &mask);

 unlock:
	dev_put(dev);
	cfg80211_unlock_rdev(rdev);
 unlock_rtnl:
	rtnl_unlock();
	return err;
}

static int nl80211_register_mgmt(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
	struct net_device *dev;
	u16 frame_type = IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_ACTION;
	int err;

	if (!info->attrs[NL80211_ATTR_FRAME_MATCH])
		return -EINVAL;

	if (info->attrs[NL80211_ATTR_FRAME_TYPE])
		frame_type = nla_get_u16(info->attrs[NL80211_ATTR_FRAME_TYPE]);

	rtnl_lock();

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto unlock_rtnl;

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_STATION &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_ADHOC &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_CLIENT) {
		err = -EOPNOTSUPP;
		goto out;
	}

	/* not much point in registering if we can't reply */
	if (!rdev->ops->mgmt_tx) {
		err = -EOPNOTSUPP;
		goto out;
	}

	err = cfg80211_mlme_register_mgmt(dev->ieee80211_ptr, info->snd_pid,
			frame_type,
			nla_data(info->attrs[NL80211_ATTR_FRAME_MATCH]),
			nla_len(info->attrs[NL80211_ATTR_FRAME_MATCH]));
 out:
	cfg80211_unlock_rdev(rdev);
	dev_put(dev);
 unlock_rtnl:
	rtnl_unlock();
	return err;
}

static int nl80211_tx_mgmt(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
	struct net_device *dev;
	struct ieee80211_channel *chan;
	enum nl80211_channel_type channel_type = NL80211_CHAN_NO_HT;
	bool channel_type_valid = false;
	u32 freq;
	int err;
	void *hdr;
	u64 cookie;
	struct sk_buff *msg;

	if (!info->attrs[NL80211_ATTR_FRAME] ||
	    !info->attrs[NL80211_ATTR_WIPHY_FREQ])
		return -EINVAL;

	rtnl_lock();

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto unlock_rtnl;

	if (!rdev->ops->mgmt_tx) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_STATION &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_ADHOC &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_CLIENT) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (!netif_running(dev)) {
		err = -ENETDOWN;
		goto out;
	}

	if (info->attrs[NL80211_ATTR_WIPHY_CHANNEL_TYPE]) {
		channel_type = nla_get_u32(
			info->attrs[NL80211_ATTR_WIPHY_CHANNEL_TYPE]);
		if (channel_type != NL80211_CHAN_NO_HT &&
		    channel_type != NL80211_CHAN_HT20 &&
		    channel_type != NL80211_CHAN_HT40PLUS &&
		    channel_type != NL80211_CHAN_HT40MINUS) {
			err = -EINVAL;
			goto out;
		}
		channel_type_valid = true;
	}

	freq = nla_get_u32(info->attrs[NL80211_ATTR_WIPHY_FREQ]);
	chan = rdev_freq_to_chan(rdev, freq, channel_type);
	if (chan == NULL) {
		err = -EINVAL;
		goto out;
	}

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		err = -ENOMEM;
		goto out;
	}

	hdr = nl80211hdr_put(msg, info->snd_pid, info->snd_seq, 0,
			     NL80211_CMD_FRAME);

	if (IS_ERR(hdr)) {
		err = PTR_ERR(hdr);
		goto free_msg;
	}
	err = cfg80211_mlme_mgmt_tx(rdev, dev, chan, channel_type,
				    channel_type_valid,
				    nla_data(info->attrs[NL80211_ATTR_FRAME]),
				    nla_len(info->attrs[NL80211_ATTR_FRAME]),
				    &cookie);
	if (err)
		goto free_msg;

	NLA_PUT_U64(msg, NL80211_ATTR_COOKIE, cookie);

	genlmsg_end(msg, hdr);
	err = genlmsg_reply(msg, info);
	goto out;

 nla_put_failure:
	err = -ENOBUFS;
 free_msg:
	nlmsg_free(msg);
 out:
	cfg80211_unlock_rdev(rdev);
	dev_put(dev);
unlock_rtnl:
	rtnl_unlock();
	return err;
}

static int nl80211_set_power_save(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
	struct wireless_dev *wdev;
	struct net_device *dev;
	u8 ps_state;
	bool state;
	int err;

	if (!info->attrs[NL80211_ATTR_PS_STATE]) {
		err = -EINVAL;
		goto out;
	}

	ps_state = nla_get_u32(info->attrs[NL80211_ATTR_PS_STATE]);

	if (ps_state != NL80211_PS_DISABLED && ps_state != NL80211_PS_ENABLED) {
		err = -EINVAL;
		goto out;
	}

	rtnl_lock();

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto unlock_rtnl;

	wdev = dev->ieee80211_ptr;

	if (!rdev->ops->set_power_mgmt) {
		err = -EOPNOTSUPP;
		goto unlock_rdev;
	}

	state = (ps_state == NL80211_PS_ENABLED) ? true : false;

	if (state == wdev->ps)
		goto unlock_rdev;

	wdev->ps = state;

	if (rdev->ops->set_power_mgmt(wdev->wiphy, dev, wdev->ps,
				      wdev->ps_timeout))
		/* assume this means it's off */
		wdev->ps = false;

unlock_rdev:
	cfg80211_unlock_rdev(rdev);
	dev_put(dev);
unlock_rtnl:
	rtnl_unlock();

out:
	return err;
}

static int nl80211_get_power_save(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
	enum nl80211_ps_state ps_state;
	struct wireless_dev *wdev;
	struct net_device *dev;
	struct sk_buff *msg;
	void *hdr;
	int err;

	rtnl_lock();

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto unlock_rtnl;

	wdev = dev->ieee80211_ptr;

	if (!rdev->ops->set_power_mgmt) {
		err = -EOPNOTSUPP;
		goto out;
	}

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		err = -ENOMEM;
		goto out;
	}

	hdr = nl80211hdr_put(msg, info->snd_pid, info->snd_seq, 0,
			     NL80211_CMD_GET_POWER_SAVE);
	if (!hdr) {
		err = -ENOMEM;
		goto free_msg;
	}

	if (wdev->ps)
		ps_state = NL80211_PS_ENABLED;
	else
		ps_state = NL80211_PS_DISABLED;

	NLA_PUT_U32(msg, NL80211_ATTR_PS_STATE, ps_state);

	genlmsg_end(msg, hdr);
	err = genlmsg_reply(msg, info);
	goto out;

nla_put_failure:
	err = -ENOBUFS;

free_msg:
	nlmsg_free(msg);

out:
	cfg80211_unlock_rdev(rdev);
	dev_put(dev);

unlock_rtnl:
	rtnl_unlock();

	return err;
}

static struct nla_policy
nl80211_attr_cqm_policy[NL80211_ATTR_CQM_MAX + 1] __read_mostly = {
	[NL80211_ATTR_CQM_RSSI_THOLD] = { .type = NLA_U32 },
	[NL80211_ATTR_CQM_RSSI_HYST] = { .type = NLA_U32 },
	[NL80211_ATTR_CQM_RSSI_THRESHOLD_EVENT] = { .type = NLA_U32 },
};

static int nl80211_set_cqm_rssi(struct genl_info *info,
				s32 threshold, u32 hysteresis)
{
	struct cfg80211_registered_device *rdev;
	struct wireless_dev *wdev;
	struct net_device *dev;
	int err;

	if (threshold > 0)
		return -EINVAL;

	rtnl_lock();

	err = get_rdev_dev_by_info_ifindex(info, &rdev, &dev);
	if (err)
		goto unlock_rdev;

	wdev = dev->ieee80211_ptr;

	if (!rdev->ops->set_cqm_rssi_config) {
		err = -EOPNOTSUPP;
		goto unlock_rdev;
	}

	if (wdev->iftype != NL80211_IFTYPE_STATION &&
	    wdev->iftype != NL80211_IFTYPE_P2P_CLIENT) {
		err = -EOPNOTSUPP;
		goto unlock_rdev;
	}

	err = rdev->ops->set_cqm_rssi_config(wdev->wiphy, dev,
					     threshold, hysteresis);

unlock_rdev:
	cfg80211_unlock_rdev(rdev);
	dev_put(dev);
	rtnl_unlock();

	return err;
}

static int nl80211_set_cqm(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *attrs[NL80211_ATTR_CQM_MAX + 1];
	struct nlattr *cqm;
	int err;

	cqm = info->attrs[NL80211_ATTR_CQM];
	if (!cqm) {
		err = -EINVAL;
		goto out;
	}

	err = nla_parse_nested(attrs, NL80211_ATTR_CQM_MAX, cqm,
			       nl80211_attr_cqm_policy);
	if (err)
		goto out;

	if (attrs[NL80211_ATTR_CQM_RSSI_THOLD] &&
	    attrs[NL80211_ATTR_CQM_RSSI_HYST]) {
		s32 threshold;
		u32 hysteresis;
		threshold = nla_get_u32(attrs[NL80211_ATTR_CQM_RSSI_THOLD]);
		hysteresis = nla_get_u32(attrs[NL80211_ATTR_CQM_RSSI_HYST]);
		err = nl80211_set_cqm_rssi(info, threshold, hysteresis);
	} else
		err = -EINVAL;

out:
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
#ifdef CONFIG_NL80211_TESTMODE
	{
		.cmd = NL80211_CMD_TESTMODE,
		.doit = nl80211_testmode_do,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
#endif
	{
		.cmd = NL80211_CMD_CONNECT,
		.doit = nl80211_connect,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_DISCONNECT,
		.doit = nl80211_disconnect,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_SET_WIPHY_NETNS,
		.doit = nl80211_wiphy_netns,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_GET_SURVEY,
		.policy = nl80211_policy,
		.dumpit = nl80211_dump_survey,
	},
	{
		.cmd = NL80211_CMD_SET_PMKSA,
		.doit = nl80211_setdel_pmksa,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_DEL_PMKSA,
		.doit = nl80211_setdel_pmksa,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_FLUSH_PMKSA,
		.doit = nl80211_flush_pmksa,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_REMAIN_ON_CHANNEL,
		.doit = nl80211_remain_on_channel,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_CANCEL_REMAIN_ON_CHANNEL,
		.doit = nl80211_cancel_remain_on_channel,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_SET_TX_BITRATE_MASK,
		.doit = nl80211_set_tx_bitrate_mask,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_REGISTER_FRAME,
		.doit = nl80211_register_mgmt,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_FRAME,
		.doit = nl80211_tx_mgmt,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_SET_POWER_SAVE,
		.doit = nl80211_set_power_save,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_GET_POWER_SAVE,
		.doit = nl80211_get_power_save,
		.policy = nl80211_policy,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = NL80211_CMD_SET_CQM,
		.doit = nl80211_set_cqm,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_SET_CHANNEL,
		.doit = nl80211_set_channel,
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

	genlmsg_multicast_netns(wiphy_net(&rdev->wiphy), msg, 0,
				nl80211_config_mcgrp.id, GFP_KERNEL);
}

static int nl80211_add_scan_req(struct sk_buff *msg,
				struct cfg80211_registered_device *rdev)
{
	struct cfg80211_scan_request *req = rdev->scan_req;
	struct nlattr *nest;
	int i;

	ASSERT_RDEV_LOCK(rdev);

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

static int nl80211_send_scan_msg(struct sk_buff *msg,
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

void nl80211_send_scan_start(struct cfg80211_registered_device *rdev,
			     struct net_device *netdev)
{
	struct sk_buff *msg;

	msg = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg)
		return;

	if (nl80211_send_scan_msg(msg, rdev, netdev, 0, 0, 0,
				  NL80211_CMD_TRIGGER_SCAN) < 0) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(wiphy_net(&rdev->wiphy), msg, 0,
				nl80211_scan_mcgrp.id, GFP_KERNEL);
}

void nl80211_send_scan_done(struct cfg80211_registered_device *rdev,
			    struct net_device *netdev)
{
	struct sk_buff *msg;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	if (nl80211_send_scan_msg(msg, rdev, netdev, 0, 0, 0,
				  NL80211_CMD_NEW_SCAN_RESULTS) < 0) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(wiphy_net(&rdev->wiphy), msg, 0,
				nl80211_scan_mcgrp.id, GFP_KERNEL);
}

void nl80211_send_scan_aborted(struct cfg80211_registered_device *rdev,
			       struct net_device *netdev)
{
	struct sk_buff *msg;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	if (nl80211_send_scan_msg(msg, rdev, netdev, 0, 0, 0,
				  NL80211_CMD_SCAN_ABORTED) < 0) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(wiphy_net(&rdev->wiphy), msg, 0,
				nl80211_scan_mcgrp.id, GFP_KERNEL);
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

	rcu_read_lock();
	genlmsg_multicast_allns(msg, 0, nl80211_regulatory_mcgrp.id,
				GFP_ATOMIC);
	rcu_read_unlock();

	return;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	nlmsg_free(msg);
}

static void nl80211_send_mlme_event(struct cfg80211_registered_device *rdev,
				    struct net_device *netdev,
				    const u8 *buf, size_t len,
				    enum nl80211_commands cmd, gfp_t gfp)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, gfp);
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

	genlmsg_multicast_netns(wiphy_net(&rdev->wiphy), msg, 0,
				nl80211_mlme_mcgrp.id, gfp);
	return;

 nla_put_failure:
	genlmsg_cancel(msg, hdr);
	nlmsg_free(msg);
}

void nl80211_send_rx_auth(struct cfg80211_registered_device *rdev,
			  struct net_device *netdev, const u8 *buf,
			  size_t len, gfp_t gfp)
{
	nl80211_send_mlme_event(rdev, netdev, buf, len,
				NL80211_CMD_AUTHENTICATE, gfp);
}

void nl80211_send_rx_assoc(struct cfg80211_registered_device *rdev,
			   struct net_device *netdev, const u8 *buf,
			   size_t len, gfp_t gfp)
{
	nl80211_send_mlme_event(rdev, netdev, buf, len,
				NL80211_CMD_ASSOCIATE, gfp);
}

void nl80211_send_deauth(struct cfg80211_registered_device *rdev,
			 struct net_device *netdev, const u8 *buf,
			 size_t len, gfp_t gfp)
{
	nl80211_send_mlme_event(rdev, netdev, buf, len,
				NL80211_CMD_DEAUTHENTICATE, gfp);
}

void nl80211_send_disassoc(struct cfg80211_registered_device *rdev,
			   struct net_device *netdev, const u8 *buf,
			   size_t len, gfp_t gfp)
{
	nl80211_send_mlme_event(rdev, netdev, buf, len,
				NL80211_CMD_DISASSOCIATE, gfp);
}

static void nl80211_send_mlme_timeout(struct cfg80211_registered_device *rdev,
				      struct net_device *netdev, int cmd,
				      const u8 *addr, gfp_t gfp)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, gfp);
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

	genlmsg_multicast_netns(wiphy_net(&rdev->wiphy), msg, 0,
				nl80211_mlme_mcgrp.id, gfp);
	return;

 nla_put_failure:
	genlmsg_cancel(msg, hdr);
	nlmsg_free(msg);
}

void nl80211_send_auth_timeout(struct cfg80211_registered_device *rdev,
			       struct net_device *netdev, const u8 *addr,
			       gfp_t gfp)
{
	nl80211_send_mlme_timeout(rdev, netdev, NL80211_CMD_AUTHENTICATE,
				  addr, gfp);
}

void nl80211_send_assoc_timeout(struct cfg80211_registered_device *rdev,
				struct net_device *netdev, const u8 *addr,
				gfp_t gfp)
{
	nl80211_send_mlme_timeout(rdev, netdev, NL80211_CMD_ASSOCIATE,
				  addr, gfp);
}

void nl80211_send_connect_result(struct cfg80211_registered_device *rdev,
				 struct net_device *netdev, const u8 *bssid,
				 const u8 *req_ie, size_t req_ie_len,
				 const u8 *resp_ie, size_t resp_ie_len,
				 u16 status, gfp_t gfp)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_GOODSIZE, gfp);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_CONNECT);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	NLA_PUT_U32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx);
	NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, netdev->ifindex);
	if (bssid)
		NLA_PUT(msg, NL80211_ATTR_MAC, ETH_ALEN, bssid);
	NLA_PUT_U16(msg, NL80211_ATTR_STATUS_CODE, status);
	if (req_ie)
		NLA_PUT(msg, NL80211_ATTR_REQ_IE, req_ie_len, req_ie);
	if (resp_ie)
		NLA_PUT(msg, NL80211_ATTR_RESP_IE, resp_ie_len, resp_ie);

	if (genlmsg_end(msg, hdr) < 0) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(wiphy_net(&rdev->wiphy), msg, 0,
				nl80211_mlme_mcgrp.id, gfp);
	return;

 nla_put_failure:
	genlmsg_cancel(msg, hdr);
	nlmsg_free(msg);

}

void nl80211_send_roamed(struct cfg80211_registered_device *rdev,
			 struct net_device *netdev, const u8 *bssid,
			 const u8 *req_ie, size_t req_ie_len,
			 const u8 *resp_ie, size_t resp_ie_len, gfp_t gfp)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_GOODSIZE, gfp);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_ROAM);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	NLA_PUT_U32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx);
	NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, netdev->ifindex);
	NLA_PUT(msg, NL80211_ATTR_MAC, ETH_ALEN, bssid);
	if (req_ie)
		NLA_PUT(msg, NL80211_ATTR_REQ_IE, req_ie_len, req_ie);
	if (resp_ie)
		NLA_PUT(msg, NL80211_ATTR_RESP_IE, resp_ie_len, resp_ie);

	if (genlmsg_end(msg, hdr) < 0) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(wiphy_net(&rdev->wiphy), msg, 0,
				nl80211_mlme_mcgrp.id, gfp);
	return;

 nla_put_failure:
	genlmsg_cancel(msg, hdr);
	nlmsg_free(msg);

}

void nl80211_send_disconnected(struct cfg80211_registered_device *rdev,
			       struct net_device *netdev, u16 reason,
			       const u8 *ie, size_t ie_len, bool from_ap)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_DISCONNECT);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	NLA_PUT_U32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx);
	NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, netdev->ifindex);
	if (from_ap && reason)
		NLA_PUT_U16(msg, NL80211_ATTR_REASON_CODE, reason);
	if (from_ap)
		NLA_PUT_FLAG(msg, NL80211_ATTR_DISCONNECTED_BY_AP);
	if (ie)
		NLA_PUT(msg, NL80211_ATTR_IE, ie_len, ie);

	if (genlmsg_end(msg, hdr) < 0) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(wiphy_net(&rdev->wiphy), msg, 0,
				nl80211_mlme_mcgrp.id, GFP_KERNEL);
	return;

 nla_put_failure:
	genlmsg_cancel(msg, hdr);
	nlmsg_free(msg);

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

	genlmsg_multicast_netns(wiphy_net(&rdev->wiphy), msg, 0,
				nl80211_mlme_mcgrp.id, gfp);
	return;

 nla_put_failure:
	genlmsg_cancel(msg, hdr);
	nlmsg_free(msg);
}

void nl80211_michael_mic_failure(struct cfg80211_registered_device *rdev,
				 struct net_device *netdev, const u8 *addr,
				 enum nl80211_key_type key_type, int key_id,
				 const u8 *tsc, gfp_t gfp)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, gfp);
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

	genlmsg_multicast_netns(wiphy_net(&rdev->wiphy), msg, 0,
				nl80211_mlme_mcgrp.id, gfp);
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

	rcu_read_lock();
	genlmsg_multicast_allns(msg, 0, nl80211_regulatory_mcgrp.id,
				GFP_ATOMIC);
	rcu_read_unlock();

	return;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	nlmsg_free(msg);
}

static void nl80211_send_remain_on_chan_event(
	int cmd, struct cfg80211_registered_device *rdev,
	struct net_device *netdev, u64 cookie,
	struct ieee80211_channel *chan,
	enum nl80211_channel_type channel_type,
	unsigned int duration, gfp_t gfp)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, gfp);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, cmd);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	NLA_PUT_U32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx);
	NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, netdev->ifindex);
	NLA_PUT_U32(msg, NL80211_ATTR_WIPHY_FREQ, chan->center_freq);
	NLA_PUT_U32(msg, NL80211_ATTR_WIPHY_CHANNEL_TYPE, channel_type);
	NLA_PUT_U64(msg, NL80211_ATTR_COOKIE, cookie);

	if (cmd == NL80211_CMD_REMAIN_ON_CHANNEL)
		NLA_PUT_U32(msg, NL80211_ATTR_DURATION, duration);

	if (genlmsg_end(msg, hdr) < 0) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(wiphy_net(&rdev->wiphy), msg, 0,
				nl80211_mlme_mcgrp.id, gfp);
	return;

 nla_put_failure:
	genlmsg_cancel(msg, hdr);
	nlmsg_free(msg);
}

void nl80211_send_remain_on_channel(struct cfg80211_registered_device *rdev,
				    struct net_device *netdev, u64 cookie,
				    struct ieee80211_channel *chan,
				    enum nl80211_channel_type channel_type,
				    unsigned int duration, gfp_t gfp)
{
	nl80211_send_remain_on_chan_event(NL80211_CMD_REMAIN_ON_CHANNEL,
					  rdev, netdev, cookie, chan,
					  channel_type, duration, gfp);
}

void nl80211_send_remain_on_channel_cancel(
	struct cfg80211_registered_device *rdev, struct net_device *netdev,
	u64 cookie, struct ieee80211_channel *chan,
	enum nl80211_channel_type channel_type, gfp_t gfp)
{
	nl80211_send_remain_on_chan_event(NL80211_CMD_CANCEL_REMAIN_ON_CHANNEL,
					  rdev, netdev, cookie, chan,
					  channel_type, 0, gfp);
}

void nl80211_send_sta_event(struct cfg80211_registered_device *rdev,
			    struct net_device *dev, const u8 *mac_addr,
			    struct station_info *sinfo, gfp_t gfp)
{
	struct sk_buff *msg;

	msg = nlmsg_new(NLMSG_GOODSIZE, gfp);
	if (!msg)
		return;

	if (nl80211_send_station(msg, 0, 0, 0, dev, mac_addr, sinfo) < 0) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(wiphy_net(&rdev->wiphy), msg, 0,
				nl80211_mlme_mcgrp.id, gfp);
}

int nl80211_send_mgmt(struct cfg80211_registered_device *rdev,
		      struct net_device *netdev, u32 nlpid,
		      int freq, const u8 *buf, size_t len, gfp_t gfp)
{
	struct sk_buff *msg;
	void *hdr;
	int err;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, gfp);
	if (!msg)
		return -ENOMEM;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_FRAME);
	if (!hdr) {
		nlmsg_free(msg);
		return -ENOMEM;
	}

	NLA_PUT_U32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx);
	NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, netdev->ifindex);
	NLA_PUT_U32(msg, NL80211_ATTR_WIPHY_FREQ, freq);
	NLA_PUT(msg, NL80211_ATTR_FRAME, len, buf);

	err = genlmsg_end(msg, hdr);
	if (err < 0) {
		nlmsg_free(msg);
		return err;
	}

	err = genlmsg_unicast(wiphy_net(&rdev->wiphy), msg, nlpid);
	if (err < 0)
		return err;
	return 0;

 nla_put_failure:
	genlmsg_cancel(msg, hdr);
	nlmsg_free(msg);
	return -ENOBUFS;
}

void nl80211_send_mgmt_tx_status(struct cfg80211_registered_device *rdev,
				 struct net_device *netdev, u64 cookie,
				 const u8 *buf, size_t len, bool ack,
				 gfp_t gfp)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, gfp);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_FRAME_TX_STATUS);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	NLA_PUT_U32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx);
	NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, netdev->ifindex);
	NLA_PUT(msg, NL80211_ATTR_FRAME, len, buf);
	NLA_PUT_U64(msg, NL80211_ATTR_COOKIE, cookie);
	if (ack)
		NLA_PUT_FLAG(msg, NL80211_ATTR_ACK);

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

void
nl80211_send_cqm_rssi_notify(struct cfg80211_registered_device *rdev,
			     struct net_device *netdev,
			     enum nl80211_cqm_rssi_threshold_event rssi_event,
			     gfp_t gfp)
{
	struct sk_buff *msg;
	struct nlattr *pinfoattr;
	void *hdr;

	msg = nlmsg_new(NLMSG_GOODSIZE, gfp);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_NOTIFY_CQM);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	NLA_PUT_U32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx);
	NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, netdev->ifindex);

	pinfoattr = nla_nest_start(msg, NL80211_ATTR_CQM);
	if (!pinfoattr)
		goto nla_put_failure;

	NLA_PUT_U32(msg, NL80211_ATTR_CQM_RSSI_THRESHOLD_EVENT,
		    rssi_event);

	nla_nest_end(msg, pinfoattr);

	if (genlmsg_end(msg, hdr) < 0) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(wiphy_net(&rdev->wiphy), msg, 0,
				nl80211_mlme_mcgrp.id, gfp);
	return;

 nla_put_failure:
	genlmsg_cancel(msg, hdr);
	nlmsg_free(msg);
}

static int nl80211_netlink_notify(struct notifier_block * nb,
				  unsigned long state,
				  void *_notify)
{
	struct netlink_notify *notify = _notify;
	struct cfg80211_registered_device *rdev;
	struct wireless_dev *wdev;

	if (state != NETLINK_URELEASE)
		return NOTIFY_DONE;

	rcu_read_lock();

	list_for_each_entry_rcu(rdev, &cfg80211_rdev_list, list)
		list_for_each_entry_rcu(wdev, &rdev->netdev_list, list)
			cfg80211_mlme_unregister_socket(wdev, notify->pid);

	rcu_read_unlock();

	return NOTIFY_DONE;
}

static struct notifier_block nl80211_netlink_notifier = {
	.notifier_call = nl80211_netlink_notify,
};

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

#ifdef CONFIG_NL80211_TESTMODE
	err = genl_register_mc_group(&nl80211_fam, &nl80211_testmode_mcgrp);
	if (err)
		goto err_out;
#endif

	err = netlink_register_notifier(&nl80211_netlink_notifier);
	if (err)
		goto err_out;

	return 0;
 err_out:
	genl_unregister_family(&nl80211_fam);
	return err;
}

void nl80211_exit(void)
{
	netlink_unregister_notifier(&nl80211_netlink_notifier);
	genl_unregister_family(&nl80211_fam);
}
