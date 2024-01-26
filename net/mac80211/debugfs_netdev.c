// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2006	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007	Johannes Berg <johannes@sipsolutions.net>
 * Copyright (C) 2020-2023 Intel Corporation
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <linux/notifier.h>
#include <net/mac80211.h>
#include <net/cfg80211.h>
#include "ieee80211_i.h"
#include "rate.h"
#include "debugfs.h"
#include "debugfs_netdev.h"
#include "driver-ops.h"

struct ieee80211_if_read_sdata_data {
	ssize_t (*format)(const struct ieee80211_sub_if_data *, char *, int);
	struct ieee80211_sub_if_data *sdata;
};

static ssize_t ieee80211_if_read_sdata_handler(struct wiphy *wiphy,
					       struct file *file,
					       char *buf,
					       size_t bufsize,
					       void *data)
{
	struct ieee80211_if_read_sdata_data *d = data;

	return d->format(d->sdata, buf, bufsize);
}

static ssize_t ieee80211_if_read_sdata(
	struct file *file,
	char __user *userbuf,
	size_t count, loff_t *ppos,
	ssize_t (*format)(const struct ieee80211_sub_if_data *sdata, char *, int))
{
	struct ieee80211_sub_if_data *sdata = file->private_data;
	struct ieee80211_if_read_sdata_data data = {
		.format = format,
		.sdata = sdata,
	};
	char buf[200];

	return wiphy_locked_debugfs_read(sdata->local->hw.wiphy,
					 file, buf, sizeof(buf),
					 userbuf, count, ppos,
					 ieee80211_if_read_sdata_handler,
					 &data);
}

struct ieee80211_if_write_sdata_data {
	ssize_t (*write)(struct ieee80211_sub_if_data *, const char *, int);
	struct ieee80211_sub_if_data *sdata;
};

static ssize_t ieee80211_if_write_sdata_handler(struct wiphy *wiphy,
						struct file *file,
						char *buf,
						size_t count,
						void *data)
{
	struct ieee80211_if_write_sdata_data *d = data;

	return d->write(d->sdata, buf, count);
}

static ssize_t ieee80211_if_write_sdata(
	struct file *file,
	const char __user *userbuf,
	size_t count, loff_t *ppos,
	ssize_t (*write)(struct ieee80211_sub_if_data *sdata, const char *, int))
{
	struct ieee80211_sub_if_data *sdata = file->private_data;
	struct ieee80211_if_write_sdata_data data = {
		.write = write,
		.sdata = sdata,
	};
	char buf[64];

	return wiphy_locked_debugfs_write(sdata->local->hw.wiphy,
					  file, buf, sizeof(buf),
					  userbuf, count,
					  ieee80211_if_write_sdata_handler,
					  &data);
}

struct ieee80211_if_read_link_data {
	ssize_t (*format)(const struct ieee80211_link_data *, char *, int);
	struct ieee80211_link_data *link;
};

static ssize_t ieee80211_if_read_link_handler(struct wiphy *wiphy,
					      struct file *file,
					      char *buf,
					      size_t bufsize,
					      void *data)
{
	struct ieee80211_if_read_link_data *d = data;

	return d->format(d->link, buf, bufsize);
}

static ssize_t ieee80211_if_read_link(
	struct file *file,
	char __user *userbuf,
	size_t count, loff_t *ppos,
	ssize_t (*format)(const struct ieee80211_link_data *link, char *, int))
{
	struct ieee80211_link_data *link = file->private_data;
	struct ieee80211_if_read_link_data data = {
		.format = format,
		.link = link,
	};
	char buf[200];

	return wiphy_locked_debugfs_read(link->sdata->local->hw.wiphy,
					 file, buf, sizeof(buf),
					 userbuf, count, ppos,
					 ieee80211_if_read_link_handler,
					 &data);
}

struct ieee80211_if_write_link_data {
	ssize_t (*write)(struct ieee80211_link_data *, const char *, int);
	struct ieee80211_link_data *link;
};

static ssize_t ieee80211_if_write_link_handler(struct wiphy *wiphy,
					       struct file *file,
					       char *buf,
					       size_t count,
					       void *data)
{
	struct ieee80211_if_write_sdata_data *d = data;

	return d->write(d->sdata, buf, count);
}

static ssize_t ieee80211_if_write_link(
	struct file *file,
	const char __user *userbuf,
	size_t count, loff_t *ppos,
	ssize_t (*write)(struct ieee80211_link_data *link, const char *, int))
{
	struct ieee80211_link_data *link = file->private_data;
	struct ieee80211_if_write_link_data data = {
		.write = write,
		.link = link,
	};
	char buf[64];

	return wiphy_locked_debugfs_write(link->sdata->local->hw.wiphy,
					  file, buf, sizeof(buf),
					  userbuf, count,
					  ieee80211_if_write_link_handler,
					  &data);
}

#define IEEE80211_IF_FMT(name, type, field, format_string)		\
static ssize_t ieee80211_if_fmt_##name(					\
	const type *data, char *buf,					\
	int buflen)							\
{									\
	return scnprintf(buf, buflen, format_string, data->field);	\
}
#define IEEE80211_IF_FMT_DEC(name, type, field)				\
		IEEE80211_IF_FMT(name, type, field, "%d\n")
#define IEEE80211_IF_FMT_HEX(name, type, field)				\
		IEEE80211_IF_FMT(name, type, field, "%#x\n")
#define IEEE80211_IF_FMT_LHEX(name, type, field)			\
		IEEE80211_IF_FMT(name, type, field, "%#lx\n")

#define IEEE80211_IF_FMT_HEXARRAY(name, type, field)			\
static ssize_t ieee80211_if_fmt_##name(					\
	const type *data,						\
	char *buf, int buflen)						\
{									\
	char *p = buf;							\
	int i;								\
	for (i = 0; i < sizeof(data->field); i++) {			\
		p += scnprintf(p, buflen + buf - p, "%.2x ",		\
				 data->field[i]);			\
	}								\
	p += scnprintf(p, buflen + buf - p, "\n");			\
	return p - buf;							\
}

#define IEEE80211_IF_FMT_ATOMIC(name, type, field)			\
static ssize_t ieee80211_if_fmt_##name(					\
	const type *data,						\
	char *buf, int buflen)						\
{									\
	return scnprintf(buf, buflen, "%d\n", atomic_read(&data->field));\
}

#define IEEE80211_IF_FMT_MAC(name, type, field)				\
static ssize_t ieee80211_if_fmt_##name(					\
	const type *data, char *buf,					\
	int buflen)							\
{									\
	return scnprintf(buf, buflen, "%pM\n", data->field);		\
}

#define IEEE80211_IF_FMT_JIFFIES_TO_MS(name, type, field)		\
static ssize_t ieee80211_if_fmt_##name(					\
	const type *data,						\
	char *buf, int buflen)						\
{									\
	return scnprintf(buf, buflen, "%d\n",				\
			 jiffies_to_msecs(data->field));		\
}

#define _IEEE80211_IF_FILE_OPS(name, _read, _write)			\
static const struct file_operations name##_ops = {			\
	.read = (_read),						\
	.write = (_write),						\
	.open = simple_open,						\
	.llseek = generic_file_llseek,					\
}

#define _IEEE80211_IF_FILE_R_FN(name)					\
static ssize_t ieee80211_if_read_##name(struct file *file,		\
					char __user *userbuf,		\
					size_t count, loff_t *ppos)	\
{									\
	return ieee80211_if_read_sdata(file,				\
				       userbuf, count, ppos,		\
				       ieee80211_if_fmt_##name);	\
}

#define _IEEE80211_IF_FILE_W_FN(name)					\
static ssize_t ieee80211_if_write_##name(struct file *file,		\
					 const char __user *userbuf,	\
					 size_t count, loff_t *ppos)	\
{									\
	return ieee80211_if_write_sdata(file, userbuf,			\
					count, ppos,			\
					ieee80211_if_parse_##name);	\
}

#define IEEE80211_IF_FILE_R(name)					\
	_IEEE80211_IF_FILE_R_FN(name)					\
	_IEEE80211_IF_FILE_OPS(name, ieee80211_if_read_##name, NULL)

#define IEEE80211_IF_FILE_W(name)					\
	_IEEE80211_IF_FILE_W_FN(name)					\
	_IEEE80211_IF_FILE_OPS(name, NULL, ieee80211_if_write_##name)

#define IEEE80211_IF_FILE_RW(name)					\
	_IEEE80211_IF_FILE_R_FN(name)					\
	_IEEE80211_IF_FILE_W_FN(name)					\
	_IEEE80211_IF_FILE_OPS(name, ieee80211_if_read_##name,		\
			       ieee80211_if_write_##name)

#define IEEE80211_IF_FILE(name, field, format)				\
	IEEE80211_IF_FMT_##format(name, struct ieee80211_sub_if_data, field) \
	IEEE80211_IF_FILE_R(name)

#define _IEEE80211_IF_LINK_R_FN(name)					\
static ssize_t ieee80211_if_read_##name(struct file *file,		\
					char __user *userbuf,		\
					size_t count, loff_t *ppos)	\
{									\
	return ieee80211_if_read_link(file,				\
				      userbuf, count, ppos,		\
				      ieee80211_if_fmt_##name);	\
}

#define _IEEE80211_IF_LINK_W_FN(name)					\
static ssize_t ieee80211_if_write_##name(struct file *file,		\
					 const char __user *userbuf,	\
					 size_t count, loff_t *ppos)	\
{									\
	return ieee80211_if_write_link(file, userbuf,			\
				       count, ppos,			\
				       ieee80211_if_parse_##name);	\
}

#define IEEE80211_IF_LINK_FILE_R(name)					\
	_IEEE80211_IF_LINK_R_FN(name)					\
	_IEEE80211_IF_FILE_OPS(link_##name, ieee80211_if_read_##name, NULL)

#define IEEE80211_IF_LINK_FILE_W(name)					\
	_IEEE80211_IF_LINK_W_FN(name)					\
	_IEEE80211_IF_FILE_OPS(link_##name, NULL, ieee80211_if_write_##name)

#define IEEE80211_IF_LINK_FILE_RW(name)					\
	_IEEE80211_IF_LINK_R_FN(name)					\
	_IEEE80211_IF_LINK_W_FN(name)					\
	_IEEE80211_IF_FILE_OPS(link_##name, ieee80211_if_read_##name,	\
			       ieee80211_if_write_##name)

#define IEEE80211_IF_LINK_FILE(name, field, format)				\
	IEEE80211_IF_FMT_##format(name, struct ieee80211_link_data, field) \
	IEEE80211_IF_LINK_FILE_R(name)

/* common attributes */
IEEE80211_IF_FILE(rc_rateidx_mask_2ghz, rc_rateidx_mask[NL80211_BAND_2GHZ],
		  HEX);
IEEE80211_IF_FILE(rc_rateidx_mask_5ghz, rc_rateidx_mask[NL80211_BAND_5GHZ],
		  HEX);
IEEE80211_IF_FILE(rc_rateidx_mcs_mask_2ghz,
		  rc_rateidx_mcs_mask[NL80211_BAND_2GHZ], HEXARRAY);
IEEE80211_IF_FILE(rc_rateidx_mcs_mask_5ghz,
		  rc_rateidx_mcs_mask[NL80211_BAND_5GHZ], HEXARRAY);

static ssize_t ieee80211_if_fmt_rc_rateidx_vht_mcs_mask_2ghz(
				const struct ieee80211_sub_if_data *sdata,
				char *buf, int buflen)
{
	int i, len = 0;
	const u16 *mask = sdata->rc_rateidx_vht_mcs_mask[NL80211_BAND_2GHZ];

	for (i = 0; i < NL80211_VHT_NSS_MAX; i++)
		len += scnprintf(buf + len, buflen - len, "%04x ", mask[i]);
	len += scnprintf(buf + len, buflen - len, "\n");

	return len;
}

IEEE80211_IF_FILE_R(rc_rateidx_vht_mcs_mask_2ghz);

static ssize_t ieee80211_if_fmt_rc_rateidx_vht_mcs_mask_5ghz(
				const struct ieee80211_sub_if_data *sdata,
				char *buf, int buflen)
{
	int i, len = 0;
	const u16 *mask = sdata->rc_rateidx_vht_mcs_mask[NL80211_BAND_5GHZ];

	for (i = 0; i < NL80211_VHT_NSS_MAX; i++)
		len += scnprintf(buf + len, buflen - len, "%04x ", mask[i]);
	len += scnprintf(buf + len, buflen - len, "\n");

	return len;
}

IEEE80211_IF_FILE_R(rc_rateidx_vht_mcs_mask_5ghz);

IEEE80211_IF_FILE(flags, flags, HEX);
IEEE80211_IF_FILE(state, state, LHEX);
IEEE80211_IF_LINK_FILE(txpower, conf->txpower, DEC);
IEEE80211_IF_LINK_FILE(ap_power_level, ap_power_level, DEC);
IEEE80211_IF_LINK_FILE(user_power_level, user_power_level, DEC);

static ssize_t
ieee80211_if_fmt_hw_queues(const struct ieee80211_sub_if_data *sdata,
			   char *buf, int buflen)
{
	int len;

	len = scnprintf(buf, buflen, "AC queues: VO:%d VI:%d BE:%d BK:%d\n",
			sdata->vif.hw_queue[IEEE80211_AC_VO],
			sdata->vif.hw_queue[IEEE80211_AC_VI],
			sdata->vif.hw_queue[IEEE80211_AC_BE],
			sdata->vif.hw_queue[IEEE80211_AC_BK]);

	if (sdata->vif.type == NL80211_IFTYPE_AP)
		len += scnprintf(buf + len, buflen - len, "cab queue: %d\n",
				 sdata->vif.cab_queue);

	return len;
}
IEEE80211_IF_FILE_R(hw_queues);

/* STA attributes */
IEEE80211_IF_FILE(bssid, deflink.u.mgd.bssid, MAC);
IEEE80211_IF_FILE(aid, vif.cfg.aid, DEC);
IEEE80211_IF_FILE(beacon_timeout, u.mgd.beacon_timeout, JIFFIES_TO_MS);

static int ieee80211_set_smps(struct ieee80211_link_data *link,
			      enum ieee80211_smps_mode smps_mode)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	struct ieee80211_local *local = sdata->local;

	/* The driver indicated that EML is enabled for the interface, thus do
	 * not allow to override the SMPS state.
	 */
	if (sdata->vif.driver_flags & IEEE80211_VIF_EML_ACTIVE)
		return -EOPNOTSUPP;

	if (!(local->hw.wiphy->features & NL80211_FEATURE_STATIC_SMPS) &&
	    smps_mode == IEEE80211_SMPS_STATIC)
		return -EINVAL;

	/* auto should be dynamic if in PS mode */
	if (!(local->hw.wiphy->features & NL80211_FEATURE_DYNAMIC_SMPS) &&
	    (smps_mode == IEEE80211_SMPS_DYNAMIC ||
	     smps_mode == IEEE80211_SMPS_AUTOMATIC))
		return -EINVAL;

	if (sdata->vif.type != NL80211_IFTYPE_STATION)
		return -EOPNOTSUPP;

	return __ieee80211_request_smps_mgd(link->sdata, link, smps_mode);
}

static const char *smps_modes[IEEE80211_SMPS_NUM_MODES] = {
	[IEEE80211_SMPS_AUTOMATIC] = "auto",
	[IEEE80211_SMPS_OFF] = "off",
	[IEEE80211_SMPS_STATIC] = "static",
	[IEEE80211_SMPS_DYNAMIC] = "dynamic",
};

static ssize_t ieee80211_if_fmt_smps(const struct ieee80211_link_data *link,
				     char *buf, int buflen)
{
	if (link->sdata->vif.type == NL80211_IFTYPE_STATION)
		return snprintf(buf, buflen, "request: %s\nused: %s\n",
				smps_modes[link->u.mgd.req_smps],
				smps_modes[link->smps_mode]);
	return -EINVAL;
}

static ssize_t ieee80211_if_parse_smps(struct ieee80211_link_data *link,
				       const char *buf, int buflen)
{
	enum ieee80211_smps_mode mode;

	for (mode = 0; mode < IEEE80211_SMPS_NUM_MODES; mode++) {
		if (strncmp(buf, smps_modes[mode], buflen) == 0) {
			int err = ieee80211_set_smps(link, mode);
			if (!err)
				return buflen;
			return err;
		}
	}

	return -EINVAL;
}
IEEE80211_IF_LINK_FILE_RW(smps);

static ssize_t ieee80211_if_parse_tkip_mic_test(
	struct ieee80211_sub_if_data *sdata, const char *buf, int buflen)
{
	struct ieee80211_local *local = sdata->local;
	u8 addr[ETH_ALEN];
	struct sk_buff *skb;
	struct ieee80211_hdr *hdr;
	__le16 fc;

	if (!mac_pton(buf, addr))
		return -EINVAL;

	if (!ieee80211_sdata_running(sdata))
		return -ENOTCONN;

	skb = dev_alloc_skb(local->hw.extra_tx_headroom + 24 + 100);
	if (!skb)
		return -ENOMEM;
	skb_reserve(skb, local->hw.extra_tx_headroom);

	hdr = skb_put_zero(skb, 24);
	fc = cpu_to_le16(IEEE80211_FTYPE_DATA | IEEE80211_STYPE_DATA);

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_AP:
		fc |= cpu_to_le16(IEEE80211_FCTL_FROMDS);
		/* DA BSSID SA */
		memcpy(hdr->addr1, addr, ETH_ALEN);
		memcpy(hdr->addr2, sdata->vif.addr, ETH_ALEN);
		memcpy(hdr->addr3, sdata->vif.addr, ETH_ALEN);
		break;
	case NL80211_IFTYPE_STATION:
		fc |= cpu_to_le16(IEEE80211_FCTL_TODS);
		/* BSSID SA DA */
		if (!sdata->u.mgd.associated) {
			dev_kfree_skb(skb);
			return -ENOTCONN;
		}
		memcpy(hdr->addr1, sdata->deflink.u.mgd.bssid, ETH_ALEN);
		memcpy(hdr->addr2, sdata->vif.addr, ETH_ALEN);
		memcpy(hdr->addr3, addr, ETH_ALEN);
		break;
	default:
		dev_kfree_skb(skb);
		return -EOPNOTSUPP;
	}
	hdr->frame_control = fc;

	/*
	 * Add some length to the test frame to make it look bit more valid.
	 * The exact contents does not matter since the recipient is required
	 * to drop this because of the Michael MIC failure.
	 */
	skb_put_zero(skb, 50);

	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_INTFL_TKIP_MIC_FAILURE;

	ieee80211_tx_skb(sdata, skb);

	return buflen;
}
IEEE80211_IF_FILE_W(tkip_mic_test);

static ssize_t ieee80211_if_parse_beacon_loss(
	struct ieee80211_sub_if_data *sdata, const char *buf, int buflen)
{
	if (!ieee80211_sdata_running(sdata) || !sdata->vif.cfg.assoc)
		return -ENOTCONN;

	ieee80211_beacon_loss(&sdata->vif);

	return buflen;
}
IEEE80211_IF_FILE_W(beacon_loss);

static ssize_t ieee80211_if_fmt_uapsd_queues(
	const struct ieee80211_sub_if_data *sdata, char *buf, int buflen)
{
	const struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	return snprintf(buf, buflen, "0x%x\n", ifmgd->uapsd_queues);
}

static ssize_t ieee80211_if_parse_uapsd_queues(
	struct ieee80211_sub_if_data *sdata, const char *buf, int buflen)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	u8 val;
	int ret;

	ret = kstrtou8(buf, 0, &val);
	if (ret)
		return ret;

	if (val & ~IEEE80211_WMM_IE_STA_QOSINFO_AC_MASK)
		return -ERANGE;

	ifmgd->uapsd_queues = val;

	return buflen;
}
IEEE80211_IF_FILE_RW(uapsd_queues);

static ssize_t ieee80211_if_fmt_uapsd_max_sp_len(
	const struct ieee80211_sub_if_data *sdata, char *buf, int buflen)
{
	const struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	return snprintf(buf, buflen, "0x%x\n", ifmgd->uapsd_max_sp_len);
}

static ssize_t ieee80211_if_parse_uapsd_max_sp_len(
	struct ieee80211_sub_if_data *sdata, const char *buf, int buflen)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 0, &val);
	if (ret)
		return -EINVAL;

	if (val & ~IEEE80211_WMM_IE_STA_QOSINFO_SP_MASK)
		return -ERANGE;

	ifmgd->uapsd_max_sp_len = val;

	return buflen;
}
IEEE80211_IF_FILE_RW(uapsd_max_sp_len);

static ssize_t ieee80211_if_fmt_tdls_wider_bw(
	const struct ieee80211_sub_if_data *sdata, char *buf, int buflen)
{
	const struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	bool tdls_wider_bw;

	tdls_wider_bw = ieee80211_hw_check(&sdata->local->hw, TDLS_WIDER_BW) &&
			!ifmgd->tdls_wider_bw_prohibited;

	return snprintf(buf, buflen, "%d\n", tdls_wider_bw);
}

static ssize_t ieee80211_if_parse_tdls_wider_bw(
	struct ieee80211_sub_if_data *sdata, const char *buf, int buflen)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	u8 val;
	int ret;

	ret = kstrtou8(buf, 0, &val);
	if (ret)
		return ret;

	ifmgd->tdls_wider_bw_prohibited = !val;
	return buflen;
}
IEEE80211_IF_FILE_RW(tdls_wider_bw);

/* AP attributes */
IEEE80211_IF_FILE(num_mcast_sta, u.ap.num_mcast_sta, ATOMIC);
IEEE80211_IF_FILE(num_sta_ps, u.ap.ps.num_sta_ps, ATOMIC);
IEEE80211_IF_FILE(dtim_count, u.ap.ps.dtim_count, DEC);
IEEE80211_IF_FILE(num_mcast_sta_vlan, u.vlan.num_mcast_sta, ATOMIC);

static ssize_t ieee80211_if_fmt_num_buffered_multicast(
	const struct ieee80211_sub_if_data *sdata, char *buf, int buflen)
{
	return scnprintf(buf, buflen, "%u\n",
			 skb_queue_len(&sdata->u.ap.ps.bc_buf));
}
IEEE80211_IF_FILE_R(num_buffered_multicast);

static ssize_t ieee80211_if_fmt_aqm(
	const struct ieee80211_sub_if_data *sdata, char *buf, int buflen)
{
	struct ieee80211_local *local = sdata->local;
	struct txq_info *txqi;
	int len;

	if (!sdata->vif.txq)
		return 0;

	txqi = to_txq_info(sdata->vif.txq);

	spin_lock_bh(&local->fq.lock);
	rcu_read_lock();

	len = scnprintf(buf,
			buflen,
			"ac backlog-bytes backlog-packets new-flows drops marks overlimit collisions tx-bytes tx-packets\n"
			"%u %u %u %u %u %u %u %u %u %u\n",
			txqi->txq.ac,
			txqi->tin.backlog_bytes,
			txqi->tin.backlog_packets,
			txqi->tin.flows,
			txqi->cstats.drop_count,
			txqi->cstats.ecn_mark,
			txqi->tin.overlimit,
			txqi->tin.collisions,
			txqi->tin.tx_bytes,
			txqi->tin.tx_packets);

	rcu_read_unlock();
	spin_unlock_bh(&local->fq.lock);

	return len;
}
IEEE80211_IF_FILE_R(aqm);

IEEE80211_IF_FILE(multicast_to_unicast, u.ap.multicast_to_unicast, HEX);

/* IBSS attributes */
static ssize_t ieee80211_if_fmt_tsf(
	const struct ieee80211_sub_if_data *sdata, char *buf, int buflen)
{
	struct ieee80211_local *local = sdata->local;
	u64 tsf;

	tsf = drv_get_tsf(local, (struct ieee80211_sub_if_data *)sdata);

	return scnprintf(buf, buflen, "0x%016llx\n", (unsigned long long) tsf);
}

static ssize_t ieee80211_if_parse_tsf(
	struct ieee80211_sub_if_data *sdata, const char *buf, int buflen)
{
	struct ieee80211_local *local = sdata->local;
	unsigned long long tsf;
	int ret;
	int tsf_is_delta = 0;

	if (strncmp(buf, "reset", 5) == 0) {
		if (local->ops->reset_tsf) {
			drv_reset_tsf(local, sdata);
			wiphy_info(local->hw.wiphy, "debugfs reset TSF\n");
		}
	} else {
		if (buflen > 10 && buf[1] == '=') {
			if (buf[0] == '+')
				tsf_is_delta = 1;
			else if (buf[0] == '-')
				tsf_is_delta = -1;
			else
				return -EINVAL;
			buf += 2;
		}
		ret = kstrtoull(buf, 10, &tsf);
		if (ret < 0)
			return ret;
		if (tsf_is_delta && local->ops->offset_tsf) {
			drv_offset_tsf(local, sdata, tsf_is_delta * tsf);
			wiphy_info(local->hw.wiphy,
				   "debugfs offset TSF by %018lld\n",
				   tsf_is_delta * tsf);
		} else if (local->ops->set_tsf) {
			if (tsf_is_delta)
				tsf = drv_get_tsf(local, sdata) +
				      tsf_is_delta * tsf;
			drv_set_tsf(local, sdata, tsf);
			wiphy_info(local->hw.wiphy,
				   "debugfs set TSF to %#018llx\n", tsf);
		}
	}

	ieee80211_recalc_dtim(local, sdata);
	return buflen;
}
IEEE80211_IF_FILE_RW(tsf);

static ssize_t ieee80211_if_fmt_valid_links(const struct ieee80211_sub_if_data *sdata,
					    char *buf, int buflen)
{
	return snprintf(buf, buflen, "0x%x\n", sdata->vif.valid_links);
}
IEEE80211_IF_FILE_R(valid_links);

static ssize_t ieee80211_if_fmt_active_links(const struct ieee80211_sub_if_data *sdata,
					     char *buf, int buflen)
{
	return snprintf(buf, buflen, "0x%x\n", sdata->vif.active_links);
}

static ssize_t ieee80211_if_parse_active_links(struct ieee80211_sub_if_data *sdata,
					       const char *buf, int buflen)
{
	u16 active_links;

	if (kstrtou16(buf, 0, &active_links))
		return -EINVAL;

	return ieee80211_set_active_links(&sdata->vif, active_links) ?: buflen;
}
IEEE80211_IF_FILE_RW(active_links);

IEEE80211_IF_LINK_FILE(addr, conf->addr, MAC);

#ifdef CONFIG_MAC80211_MESH
IEEE80211_IF_FILE(estab_plinks, u.mesh.estab_plinks, ATOMIC);

/* Mesh stats attributes */
IEEE80211_IF_FILE(fwded_mcast, u.mesh.mshstats.fwded_mcast, DEC);
IEEE80211_IF_FILE(fwded_unicast, u.mesh.mshstats.fwded_unicast, DEC);
IEEE80211_IF_FILE(fwded_frames, u.mesh.mshstats.fwded_frames, DEC);
IEEE80211_IF_FILE(dropped_frames_ttl, u.mesh.mshstats.dropped_frames_ttl, DEC);
IEEE80211_IF_FILE(dropped_frames_no_route,
		  u.mesh.mshstats.dropped_frames_no_route, DEC);

/* Mesh parameters */
IEEE80211_IF_FILE(dot11MeshMaxRetries,
		  u.mesh.mshcfg.dot11MeshMaxRetries, DEC);
IEEE80211_IF_FILE(dot11MeshRetryTimeout,
		  u.mesh.mshcfg.dot11MeshRetryTimeout, DEC);
IEEE80211_IF_FILE(dot11MeshConfirmTimeout,
		  u.mesh.mshcfg.dot11MeshConfirmTimeout, DEC);
IEEE80211_IF_FILE(dot11MeshHoldingTimeout,
		  u.mesh.mshcfg.dot11MeshHoldingTimeout, DEC);
IEEE80211_IF_FILE(dot11MeshTTL, u.mesh.mshcfg.dot11MeshTTL, DEC);
IEEE80211_IF_FILE(element_ttl, u.mesh.mshcfg.element_ttl, DEC);
IEEE80211_IF_FILE(auto_open_plinks, u.mesh.mshcfg.auto_open_plinks, DEC);
IEEE80211_IF_FILE(dot11MeshMaxPeerLinks,
		  u.mesh.mshcfg.dot11MeshMaxPeerLinks, DEC);
IEEE80211_IF_FILE(dot11MeshHWMPactivePathTimeout,
		  u.mesh.mshcfg.dot11MeshHWMPactivePathTimeout, DEC);
IEEE80211_IF_FILE(dot11MeshHWMPpreqMinInterval,
		  u.mesh.mshcfg.dot11MeshHWMPpreqMinInterval, DEC);
IEEE80211_IF_FILE(dot11MeshHWMPperrMinInterval,
		  u.mesh.mshcfg.dot11MeshHWMPperrMinInterval, DEC);
IEEE80211_IF_FILE(dot11MeshHWMPnetDiameterTraversalTime,
		  u.mesh.mshcfg.dot11MeshHWMPnetDiameterTraversalTime, DEC);
IEEE80211_IF_FILE(dot11MeshHWMPmaxPREQretries,
		  u.mesh.mshcfg.dot11MeshHWMPmaxPREQretries, DEC);
IEEE80211_IF_FILE(path_refresh_time,
		  u.mesh.mshcfg.path_refresh_time, DEC);
IEEE80211_IF_FILE(min_discovery_timeout,
		  u.mesh.mshcfg.min_discovery_timeout, DEC);
IEEE80211_IF_FILE(dot11MeshHWMPRootMode,
		  u.mesh.mshcfg.dot11MeshHWMPRootMode, DEC);
IEEE80211_IF_FILE(dot11MeshGateAnnouncementProtocol,
		  u.mesh.mshcfg.dot11MeshGateAnnouncementProtocol, DEC);
IEEE80211_IF_FILE(dot11MeshHWMPRannInterval,
		  u.mesh.mshcfg.dot11MeshHWMPRannInterval, DEC);
IEEE80211_IF_FILE(dot11MeshForwarding, u.mesh.mshcfg.dot11MeshForwarding, DEC);
IEEE80211_IF_FILE(rssi_threshold, u.mesh.mshcfg.rssi_threshold, DEC);
IEEE80211_IF_FILE(ht_opmode, u.mesh.mshcfg.ht_opmode, DEC);
IEEE80211_IF_FILE(dot11MeshHWMPactivePathToRootTimeout,
		  u.mesh.mshcfg.dot11MeshHWMPactivePathToRootTimeout, DEC);
IEEE80211_IF_FILE(dot11MeshHWMProotInterval,
		  u.mesh.mshcfg.dot11MeshHWMProotInterval, DEC);
IEEE80211_IF_FILE(dot11MeshHWMPconfirmationInterval,
		  u.mesh.mshcfg.dot11MeshHWMPconfirmationInterval, DEC);
IEEE80211_IF_FILE(power_mode, u.mesh.mshcfg.power_mode, DEC);
IEEE80211_IF_FILE(dot11MeshAwakeWindowDuration,
		  u.mesh.mshcfg.dot11MeshAwakeWindowDuration, DEC);
IEEE80211_IF_FILE(dot11MeshConnectedToMeshGate,
		  u.mesh.mshcfg.dot11MeshConnectedToMeshGate, DEC);
IEEE80211_IF_FILE(dot11MeshNolearn, u.mesh.mshcfg.dot11MeshNolearn, DEC);
IEEE80211_IF_FILE(dot11MeshConnectedToAuthServer,
		  u.mesh.mshcfg.dot11MeshConnectedToAuthServer, DEC);
#endif

#define DEBUGFS_ADD_MODE(name, mode) \
	debugfs_create_file(#name, mode, sdata->vif.debugfs_dir, \
			    sdata, &name##_ops)

#define DEBUGFS_ADD_X(_bits, _name, _mode) \
	debugfs_create_x##_bits(#_name, _mode, sdata->vif.debugfs_dir, \
				&sdata->vif._name)

#define DEBUGFS_ADD_X8(_name, _mode) \
	DEBUGFS_ADD_X(8, _name, _mode)

#define DEBUGFS_ADD_X16(_name, _mode) \
	DEBUGFS_ADD_X(16, _name, _mode)

#define DEBUGFS_ADD_X32(_name, _mode) \
	DEBUGFS_ADD_X(32, _name, _mode)

#define DEBUGFS_ADD(name) DEBUGFS_ADD_MODE(name, 0400)

static void add_common_files(struct ieee80211_sub_if_data *sdata)
{
	DEBUGFS_ADD(rc_rateidx_mask_2ghz);
	DEBUGFS_ADD(rc_rateidx_mask_5ghz);
	DEBUGFS_ADD(rc_rateidx_mcs_mask_2ghz);
	DEBUGFS_ADD(rc_rateidx_mcs_mask_5ghz);
	DEBUGFS_ADD(rc_rateidx_vht_mcs_mask_2ghz);
	DEBUGFS_ADD(rc_rateidx_vht_mcs_mask_5ghz);
	DEBUGFS_ADD(hw_queues);

	if (sdata->vif.type != NL80211_IFTYPE_P2P_DEVICE &&
	    sdata->vif.type != NL80211_IFTYPE_NAN)
		DEBUGFS_ADD(aqm);
}

static void add_sta_files(struct ieee80211_sub_if_data *sdata)
{
	DEBUGFS_ADD(bssid);
	DEBUGFS_ADD(aid);
	DEBUGFS_ADD(beacon_timeout);
	DEBUGFS_ADD_MODE(tkip_mic_test, 0200);
	DEBUGFS_ADD_MODE(beacon_loss, 0200);
	DEBUGFS_ADD_MODE(uapsd_queues, 0600);
	DEBUGFS_ADD_MODE(uapsd_max_sp_len, 0600);
	DEBUGFS_ADD_MODE(tdls_wider_bw, 0600);
	DEBUGFS_ADD_MODE(valid_links, 0400);
	DEBUGFS_ADD_MODE(active_links, 0600);
	DEBUGFS_ADD_X16(dormant_links, 0400);
}

static void add_ap_files(struct ieee80211_sub_if_data *sdata)
{
	DEBUGFS_ADD(num_mcast_sta);
	DEBUGFS_ADD(num_sta_ps);
	DEBUGFS_ADD(dtim_count);
	DEBUGFS_ADD(num_buffered_multicast);
	DEBUGFS_ADD_MODE(tkip_mic_test, 0200);
	DEBUGFS_ADD_MODE(multicast_to_unicast, 0600);
}

static void add_vlan_files(struct ieee80211_sub_if_data *sdata)
{
	/* add num_mcast_sta_vlan using name num_mcast_sta */
	debugfs_create_file("num_mcast_sta", 0400, sdata->vif.debugfs_dir,
			    sdata, &num_mcast_sta_vlan_ops);
}

static void add_ibss_files(struct ieee80211_sub_if_data *sdata)
{
	DEBUGFS_ADD_MODE(tsf, 0600);
}

#ifdef CONFIG_MAC80211_MESH

static void add_mesh_files(struct ieee80211_sub_if_data *sdata)
{
	DEBUGFS_ADD_MODE(tsf, 0600);
	DEBUGFS_ADD_MODE(estab_plinks, 0400);
}

static void add_mesh_stats(struct ieee80211_sub_if_data *sdata)
{
	struct dentry *dir = debugfs_create_dir("mesh_stats",
						sdata->vif.debugfs_dir);
#define MESHSTATS_ADD(name)\
	debugfs_create_file(#name, 0400, dir, sdata, &name##_ops)

	MESHSTATS_ADD(fwded_mcast);
	MESHSTATS_ADD(fwded_unicast);
	MESHSTATS_ADD(fwded_frames);
	MESHSTATS_ADD(dropped_frames_ttl);
	MESHSTATS_ADD(dropped_frames_no_route);
#undef MESHSTATS_ADD
}

static void add_mesh_config(struct ieee80211_sub_if_data *sdata)
{
	struct dentry *dir = debugfs_create_dir("mesh_config",
						sdata->vif.debugfs_dir);

#define MESHPARAMS_ADD(name) \
	debugfs_create_file(#name, 0600, dir, sdata, &name##_ops)

	MESHPARAMS_ADD(dot11MeshMaxRetries);
	MESHPARAMS_ADD(dot11MeshRetryTimeout);
	MESHPARAMS_ADD(dot11MeshConfirmTimeout);
	MESHPARAMS_ADD(dot11MeshHoldingTimeout);
	MESHPARAMS_ADD(dot11MeshTTL);
	MESHPARAMS_ADD(element_ttl);
	MESHPARAMS_ADD(auto_open_plinks);
	MESHPARAMS_ADD(dot11MeshMaxPeerLinks);
	MESHPARAMS_ADD(dot11MeshHWMPactivePathTimeout);
	MESHPARAMS_ADD(dot11MeshHWMPpreqMinInterval);
	MESHPARAMS_ADD(dot11MeshHWMPperrMinInterval);
	MESHPARAMS_ADD(dot11MeshHWMPnetDiameterTraversalTime);
	MESHPARAMS_ADD(dot11MeshHWMPmaxPREQretries);
	MESHPARAMS_ADD(path_refresh_time);
	MESHPARAMS_ADD(min_discovery_timeout);
	MESHPARAMS_ADD(dot11MeshHWMPRootMode);
	MESHPARAMS_ADD(dot11MeshHWMPRannInterval);
	MESHPARAMS_ADD(dot11MeshForwarding);
	MESHPARAMS_ADD(dot11MeshGateAnnouncementProtocol);
	MESHPARAMS_ADD(rssi_threshold);
	MESHPARAMS_ADD(ht_opmode);
	MESHPARAMS_ADD(dot11MeshHWMPactivePathToRootTimeout);
	MESHPARAMS_ADD(dot11MeshHWMProotInterval);
	MESHPARAMS_ADD(dot11MeshHWMPconfirmationInterval);
	MESHPARAMS_ADD(power_mode);
	MESHPARAMS_ADD(dot11MeshAwakeWindowDuration);
	MESHPARAMS_ADD(dot11MeshConnectedToMeshGate);
	MESHPARAMS_ADD(dot11MeshNolearn);
	MESHPARAMS_ADD(dot11MeshConnectedToAuthServer);
#undef MESHPARAMS_ADD
}
#endif

static void add_files(struct ieee80211_sub_if_data *sdata)
{
	if (!sdata->vif.debugfs_dir)
		return;

	DEBUGFS_ADD(flags);
	DEBUGFS_ADD(state);

	if (sdata->vif.type != NL80211_IFTYPE_MONITOR)
		add_common_files(sdata);

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_MESH_POINT:
#ifdef CONFIG_MAC80211_MESH
		add_mesh_files(sdata);
		add_mesh_stats(sdata);
		add_mesh_config(sdata);
#endif
		break;
	case NL80211_IFTYPE_STATION:
		add_sta_files(sdata);
		break;
	case NL80211_IFTYPE_ADHOC:
		add_ibss_files(sdata);
		break;
	case NL80211_IFTYPE_AP:
		add_ap_files(sdata);
		break;
	case NL80211_IFTYPE_AP_VLAN:
		add_vlan_files(sdata);
		break;
	default:
		break;
	}
}

#undef DEBUGFS_ADD_MODE
#undef DEBUGFS_ADD

#define DEBUGFS_ADD_MODE(dentry, name, mode) \
	debugfs_create_file(#name, mode, dentry, \
			    link, &link_##name##_ops)

#define DEBUGFS_ADD(dentry, name) DEBUGFS_ADD_MODE(dentry, name, 0400)

static void add_link_files(struct ieee80211_link_data *link,
			   struct dentry *dentry)
{
	DEBUGFS_ADD(dentry, txpower);
	DEBUGFS_ADD(dentry, user_power_level);
	DEBUGFS_ADD(dentry, ap_power_level);

	switch (link->sdata->vif.type) {
	case NL80211_IFTYPE_STATION:
		DEBUGFS_ADD_MODE(dentry, smps, 0600);
		break;
	default:
		break;
	}
}

void ieee80211_debugfs_add_netdev(struct ieee80211_sub_if_data *sdata,
				  bool mld_vif)
{
	char buf[10+IFNAMSIZ];

	sprintf(buf, "netdev:%s", sdata->name);
	sdata->vif.debugfs_dir = debugfs_create_dir(buf,
		sdata->local->hw.wiphy->debugfsdir);
	/* deflink also has this */
	sdata->deflink.debugfs_dir = sdata->vif.debugfs_dir;
	sdata->debugfs.subdir_stations = debugfs_create_dir("stations",
							sdata->vif.debugfs_dir);
	add_files(sdata);
	if (!mld_vif)
		add_link_files(&sdata->deflink, sdata->vif.debugfs_dir);
}

void ieee80211_debugfs_remove_netdev(struct ieee80211_sub_if_data *sdata)
{
	if (!sdata->vif.debugfs_dir)
		return;

	debugfs_remove_recursive(sdata->vif.debugfs_dir);
	sdata->vif.debugfs_dir = NULL;
	sdata->debugfs.subdir_stations = NULL;
}

void ieee80211_debugfs_rename_netdev(struct ieee80211_sub_if_data *sdata)
{
	struct dentry *dir;
	char buf[10 + IFNAMSIZ];

	dir = sdata->vif.debugfs_dir;

	if (IS_ERR_OR_NULL(dir))
		return;

	sprintf(buf, "netdev:%s", sdata->name);
	debugfs_rename(dir->d_parent, dir, dir->d_parent, buf);
}

void ieee80211_debugfs_recreate_netdev(struct ieee80211_sub_if_data *sdata,
				       bool mld_vif)
{
	ieee80211_debugfs_remove_netdev(sdata);
	ieee80211_debugfs_add_netdev(sdata, mld_vif);

	if (sdata->flags & IEEE80211_SDATA_IN_DRIVER) {
		drv_vif_add_debugfs(sdata->local, sdata);
		if (!mld_vif)
			ieee80211_link_debugfs_drv_add(&sdata->deflink);
	}
}

void ieee80211_link_debugfs_add(struct ieee80211_link_data *link)
{
	char link_dir_name[10];

	if (WARN_ON(!link->sdata->vif.debugfs_dir || link->debugfs_dir))
		return;

	/* For now, this should not be called for non-MLO capable drivers */
	if (WARN_ON(!(link->sdata->local->hw.wiphy->flags & WIPHY_FLAG_SUPPORTS_MLO)))
		return;

	snprintf(link_dir_name, sizeof(link_dir_name),
		 "link-%d", link->link_id);

	link->debugfs_dir =
		debugfs_create_dir(link_dir_name,
				   link->sdata->vif.debugfs_dir);

	DEBUGFS_ADD(link->debugfs_dir, addr);
	add_link_files(link, link->debugfs_dir);
}

void ieee80211_link_debugfs_remove(struct ieee80211_link_data *link)
{
	if (!link->sdata->vif.debugfs_dir || !link->debugfs_dir) {
		link->debugfs_dir = NULL;
		return;
	}

	if (link->debugfs_dir == link->sdata->vif.debugfs_dir) {
		WARN_ON(link != &link->sdata->deflink);
		link->debugfs_dir = NULL;
		return;
	}

	debugfs_remove_recursive(link->debugfs_dir);
	link->debugfs_dir = NULL;
}

void ieee80211_link_debugfs_drv_add(struct ieee80211_link_data *link)
{
	if (link->sdata->vif.type == NL80211_IFTYPE_MONITOR ||
	    WARN_ON(!link->debugfs_dir))
		return;

	drv_link_add_debugfs(link->sdata->local, link->sdata,
			     link->conf, link->debugfs_dir);
}

void ieee80211_link_debugfs_drv_remove(struct ieee80211_link_data *link)
{
	if (!link || !link->debugfs_dir)
		return;

	if (WARN_ON(link->debugfs_dir == link->sdata->vif.debugfs_dir))
		return;

	/* Recreate the directory excluding the driver data */
	debugfs_remove_recursive(link->debugfs_dir);
	link->debugfs_dir = NULL;

	ieee80211_link_debugfs_add(link);
}
