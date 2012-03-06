/*
 * Copyright (c) 2006	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007	Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/if.h>
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

static ssize_t ieee80211_if_read(
	struct ieee80211_sub_if_data *sdata,
	char __user *userbuf,
	size_t count, loff_t *ppos,
	ssize_t (*format)(const struct ieee80211_sub_if_data *, char *, int))
{
	char buf[70];
	ssize_t ret = -EINVAL;

	read_lock(&dev_base_lock);
	if (sdata->dev->reg_state == NETREG_REGISTERED)
		ret = (*format)(sdata, buf, sizeof(buf));
	read_unlock(&dev_base_lock);

	if (ret >= 0)
		ret = simple_read_from_buffer(userbuf, count, ppos, buf, ret);

	return ret;
}

static ssize_t ieee80211_if_write(
	struct ieee80211_sub_if_data *sdata,
	const char __user *userbuf,
	size_t count, loff_t *ppos,
	ssize_t (*write)(struct ieee80211_sub_if_data *, const char *, int))
{
	u8 *buf;
	ssize_t ret;

	buf = kmalloc(count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = -EFAULT;
	if (copy_from_user(buf, userbuf, count))
		goto freebuf;

	ret = -ENODEV;
	rtnl_lock();
	if (sdata->dev->reg_state == NETREG_REGISTERED)
		ret = (*write)(sdata, buf, count);
	rtnl_unlock();

freebuf:
	kfree(buf);
	return ret;
}

#define IEEE80211_IF_FMT(name, field, format_string)			\
static ssize_t ieee80211_if_fmt_##name(					\
	const struct ieee80211_sub_if_data *sdata, char *buf,		\
	int buflen)							\
{									\
	return scnprintf(buf, buflen, format_string, sdata->field);	\
}
#define IEEE80211_IF_FMT_DEC(name, field)				\
		IEEE80211_IF_FMT(name, field, "%d\n")
#define IEEE80211_IF_FMT_HEX(name, field)				\
		IEEE80211_IF_FMT(name, field, "%#x\n")
#define IEEE80211_IF_FMT_LHEX(name, field)				\
		IEEE80211_IF_FMT(name, field, "%#lx\n")
#define IEEE80211_IF_FMT_SIZE(name, field)				\
		IEEE80211_IF_FMT(name, field, "%zd\n")

#define IEEE80211_IF_FMT_HEXARRAY(name, field)				\
static ssize_t ieee80211_if_fmt_##name(					\
	const struct ieee80211_sub_if_data *sdata,			\
	char *buf, int buflen)						\
{									\
	char *p = buf;							\
	int i;								\
	for (i = 0; i < sizeof(sdata->field); i++) {			\
		p += scnprintf(p, buflen + buf - p, "%.2x ",		\
				 sdata->field[i]);			\
	}								\
	p += scnprintf(p, buflen + buf - p, "\n");			\
	return p - buf;							\
}

#define IEEE80211_IF_FMT_ATOMIC(name, field)				\
static ssize_t ieee80211_if_fmt_##name(					\
	const struct ieee80211_sub_if_data *sdata,			\
	char *buf, int buflen)						\
{									\
	return scnprintf(buf, buflen, "%d\n", atomic_read(&sdata->field));\
}

#define IEEE80211_IF_FMT_MAC(name, field)				\
static ssize_t ieee80211_if_fmt_##name(					\
	const struct ieee80211_sub_if_data *sdata, char *buf,		\
	int buflen)							\
{									\
	return scnprintf(buf, buflen, "%pM\n", sdata->field);		\
}

#define IEEE80211_IF_FMT_DEC_DIV_16(name, field)			\
static ssize_t ieee80211_if_fmt_##name(					\
	const struct ieee80211_sub_if_data *sdata,			\
	char *buf, int buflen)						\
{									\
	return scnprintf(buf, buflen, "%d\n", sdata->field / 16);	\
}

#define __IEEE80211_IF_FILE(name, _write)				\
static ssize_t ieee80211_if_read_##name(struct file *file,		\
					char __user *userbuf,		\
					size_t count, loff_t *ppos)	\
{									\
	return ieee80211_if_read(file->private_data,			\
				 userbuf, count, ppos,			\
				 ieee80211_if_fmt_##name);		\
}									\
static const struct file_operations name##_ops = {			\
	.read = ieee80211_if_read_##name,				\
	.write = (_write),						\
	.open = mac80211_open_file_generic,				\
	.llseek = generic_file_llseek,					\
}

#define __IEEE80211_IF_FILE_W(name)					\
static ssize_t ieee80211_if_write_##name(struct file *file,		\
					 const char __user *userbuf,	\
					 size_t count, loff_t *ppos)	\
{									\
	return ieee80211_if_write(file->private_data, userbuf, count,	\
				  ppos, ieee80211_if_parse_##name);	\
}									\
__IEEE80211_IF_FILE(name, ieee80211_if_write_##name)


#define IEEE80211_IF_FILE(name, field, format)				\
		IEEE80211_IF_FMT_##format(name, field)			\
		__IEEE80211_IF_FILE(name, NULL)

/* common attributes */
IEEE80211_IF_FILE(drop_unencrypted, drop_unencrypted, DEC);
IEEE80211_IF_FILE(rc_rateidx_mask_2ghz, rc_rateidx_mask[IEEE80211_BAND_2GHZ],
		  HEX);
IEEE80211_IF_FILE(rc_rateidx_mask_5ghz, rc_rateidx_mask[IEEE80211_BAND_5GHZ],
		  HEX);
IEEE80211_IF_FILE(rc_rateidx_mcs_mask_2ghz,
		  rc_rateidx_mcs_mask[IEEE80211_BAND_2GHZ], HEXARRAY);
IEEE80211_IF_FILE(rc_rateidx_mcs_mask_5ghz,
		  rc_rateidx_mcs_mask[IEEE80211_BAND_5GHZ], HEXARRAY);

IEEE80211_IF_FILE(flags, flags, HEX);
IEEE80211_IF_FILE(state, state, LHEX);
IEEE80211_IF_FILE(channel_type, vif.bss_conf.channel_type, DEC);

/* STA attributes */
IEEE80211_IF_FILE(bssid, u.mgd.bssid, MAC);
IEEE80211_IF_FILE(aid, u.mgd.aid, DEC);
IEEE80211_IF_FILE(last_beacon, u.mgd.last_beacon_signal, DEC);
IEEE80211_IF_FILE(ave_beacon, u.mgd.ave_beacon_signal, DEC_DIV_16);

static int ieee80211_set_smps(struct ieee80211_sub_if_data *sdata,
			      enum ieee80211_smps_mode smps_mode)
{
	struct ieee80211_local *local = sdata->local;
	int err;

	if (!(local->hw.flags & IEEE80211_HW_SUPPORTS_STATIC_SMPS) &&
	    smps_mode == IEEE80211_SMPS_STATIC)
		return -EINVAL;

	/* auto should be dynamic if in PS mode */
	if (!(local->hw.flags & IEEE80211_HW_SUPPORTS_DYNAMIC_SMPS) &&
	    (smps_mode == IEEE80211_SMPS_DYNAMIC ||
	     smps_mode == IEEE80211_SMPS_AUTOMATIC))
		return -EINVAL;

	/* supported only on managed interfaces for now */
	if (sdata->vif.type != NL80211_IFTYPE_STATION)
		return -EOPNOTSUPP;

	mutex_lock(&sdata->u.mgd.mtx);
	err = __ieee80211_request_smps(sdata, smps_mode);
	mutex_unlock(&sdata->u.mgd.mtx);

	return err;
}

static const char *smps_modes[IEEE80211_SMPS_NUM_MODES] = {
	[IEEE80211_SMPS_AUTOMATIC] = "auto",
	[IEEE80211_SMPS_OFF] = "off",
	[IEEE80211_SMPS_STATIC] = "static",
	[IEEE80211_SMPS_DYNAMIC] = "dynamic",
};

static ssize_t ieee80211_if_fmt_smps(const struct ieee80211_sub_if_data *sdata,
				     char *buf, int buflen)
{
	if (sdata->vif.type != NL80211_IFTYPE_STATION)
		return -EOPNOTSUPP;

	return snprintf(buf, buflen, "request: %s\nused: %s\n",
			smps_modes[sdata->u.mgd.req_smps],
			smps_modes[sdata->u.mgd.ap_smps]);
}

static ssize_t ieee80211_if_parse_smps(struct ieee80211_sub_if_data *sdata,
				       const char *buf, int buflen)
{
	enum ieee80211_smps_mode mode;

	for (mode = 0; mode < IEEE80211_SMPS_NUM_MODES; mode++) {
		if (strncmp(buf, smps_modes[mode], buflen) == 0) {
			int err = ieee80211_set_smps(sdata, mode);
			if (!err)
				return buflen;
			return err;
		}
	}

	return -EINVAL;
}

__IEEE80211_IF_FILE_W(smps);

static ssize_t ieee80211_if_fmt_tkip_mic_test(
	const struct ieee80211_sub_if_data *sdata, char *buf, int buflen)
{
	return -EOPNOTSUPP;
}

static int hwaddr_aton(const char *txt, u8 *addr)
{
	int i;

	for (i = 0; i < ETH_ALEN; i++) {
		int a, b;

		a = hex_to_bin(*txt++);
		if (a < 0)
			return -1;
		b = hex_to_bin(*txt++);
		if (b < 0)
			return -1;
		*addr++ = (a << 4) | b;
		if (i < 5 && *txt++ != ':')
			return -1;
	}

	return 0;
}

static ssize_t ieee80211_if_parse_tkip_mic_test(
	struct ieee80211_sub_if_data *sdata, const char *buf, int buflen)
{
	struct ieee80211_local *local = sdata->local;
	u8 addr[ETH_ALEN];
	struct sk_buff *skb;
	struct ieee80211_hdr *hdr;
	__le16 fc;

	/*
	 * Assume colon-delimited MAC address with possible white space
	 * following.
	 */
	if (buflen < 3 * ETH_ALEN - 1)
		return -EINVAL;
	if (hwaddr_aton(buf, addr) < 0)
		return -EINVAL;

	if (!ieee80211_sdata_running(sdata))
		return -ENOTCONN;

	skb = dev_alloc_skb(local->hw.extra_tx_headroom + 24 + 100);
	if (!skb)
		return -ENOMEM;
	skb_reserve(skb, local->hw.extra_tx_headroom);

	hdr = (struct ieee80211_hdr *) skb_put(skb, 24);
	memset(hdr, 0, 24);
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
		if (sdata->vif.bss_conf.bssid == NULL) {
			dev_kfree_skb(skb);
			return -ENOTCONN;
		}
		memcpy(hdr->addr1, sdata->vif.bss_conf.bssid, ETH_ALEN);
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
	memset(skb_put(skb, 50), 0, 50);

	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_INTFL_TKIP_MIC_FAILURE;

	ieee80211_tx_skb(sdata, skb);

	return buflen;
}

__IEEE80211_IF_FILE_W(tkip_mic_test);

/* AP attributes */
IEEE80211_IF_FILE(num_sta_authorized, u.ap.num_sta_authorized, ATOMIC);
IEEE80211_IF_FILE(num_sta_ps, u.ap.num_sta_ps, ATOMIC);
IEEE80211_IF_FILE(dtim_count, u.ap.dtim_count, DEC);

static ssize_t ieee80211_if_fmt_num_buffered_multicast(
	const struct ieee80211_sub_if_data *sdata, char *buf, int buflen)
{
	return scnprintf(buf, buflen, "%u\n",
			 skb_queue_len(&sdata->u.ap.ps_bc_buf));
}
__IEEE80211_IF_FILE(num_buffered_multicast, NULL);

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

	if (strncmp(buf, "reset", 5) == 0) {
		if (local->ops->reset_tsf) {
			drv_reset_tsf(local, sdata);
			wiphy_info(local->hw.wiphy, "debugfs reset TSF\n");
		}
	} else {
		ret = kstrtoull(buf, 10, &tsf);
		if (ret < 0)
			return -EINVAL;
		if (local->ops->set_tsf) {
			drv_set_tsf(local, sdata, tsf);
			wiphy_info(local->hw.wiphy,
				   "debugfs set TSF to %#018llx\n", tsf);
		}
	}

	return buflen;
}
__IEEE80211_IF_FILE_W(tsf);


/* WDS attributes */
IEEE80211_IF_FILE(peer, u.wds.remote_addr, MAC);

#ifdef CONFIG_MAC80211_MESH
/* Mesh stats attributes */
IEEE80211_IF_FILE(fwded_mcast, u.mesh.mshstats.fwded_mcast, DEC);
IEEE80211_IF_FILE(fwded_unicast, u.mesh.mshstats.fwded_unicast, DEC);
IEEE80211_IF_FILE(fwded_frames, u.mesh.mshstats.fwded_frames, DEC);
IEEE80211_IF_FILE(dropped_frames_ttl, u.mesh.mshstats.dropped_frames_ttl, DEC);
IEEE80211_IF_FILE(dropped_frames_congestion,
		u.mesh.mshstats.dropped_frames_congestion, DEC);
IEEE80211_IF_FILE(dropped_frames_no_route,
		u.mesh.mshstats.dropped_frames_no_route, DEC);
IEEE80211_IF_FILE(estab_plinks, u.mesh.mshstats.estab_plinks, ATOMIC);

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
#endif


#define DEBUGFS_ADD(name) \
	debugfs_create_file(#name, 0400, sdata->debugfs.dir, \
			    sdata, &name##_ops);

#define DEBUGFS_ADD_MODE(name, mode) \
	debugfs_create_file(#name, mode, sdata->debugfs.dir, \
			    sdata, &name##_ops);

static void add_sta_files(struct ieee80211_sub_if_data *sdata)
{
	DEBUGFS_ADD(drop_unencrypted);
	DEBUGFS_ADD(flags);
	DEBUGFS_ADD(state);
	DEBUGFS_ADD(channel_type);
	DEBUGFS_ADD(rc_rateidx_mask_2ghz);
	DEBUGFS_ADD(rc_rateidx_mask_5ghz);
	DEBUGFS_ADD(rc_rateidx_mcs_mask_2ghz);
	DEBUGFS_ADD(rc_rateidx_mcs_mask_5ghz);

	DEBUGFS_ADD(bssid);
	DEBUGFS_ADD(aid);
	DEBUGFS_ADD(last_beacon);
	DEBUGFS_ADD(ave_beacon);
	DEBUGFS_ADD_MODE(smps, 0600);
	DEBUGFS_ADD_MODE(tkip_mic_test, 0200);
}

static void add_ap_files(struct ieee80211_sub_if_data *sdata)
{
	DEBUGFS_ADD(drop_unencrypted);
	DEBUGFS_ADD(flags);
	DEBUGFS_ADD(state);
	DEBUGFS_ADD(channel_type);
	DEBUGFS_ADD(rc_rateidx_mask_2ghz);
	DEBUGFS_ADD(rc_rateidx_mask_5ghz);
	DEBUGFS_ADD(rc_rateidx_mcs_mask_2ghz);
	DEBUGFS_ADD(rc_rateidx_mcs_mask_5ghz);

	DEBUGFS_ADD(num_sta_authorized);
	DEBUGFS_ADD(num_sta_ps);
	DEBUGFS_ADD(dtim_count);
	DEBUGFS_ADD(num_buffered_multicast);
	DEBUGFS_ADD_MODE(tkip_mic_test, 0200);
}

static void add_ibss_files(struct ieee80211_sub_if_data *sdata)
{
	DEBUGFS_ADD(channel_type);
	DEBUGFS_ADD(rc_rateidx_mask_2ghz);
	DEBUGFS_ADD(rc_rateidx_mask_5ghz);
	DEBUGFS_ADD(rc_rateidx_mcs_mask_2ghz);
	DEBUGFS_ADD(rc_rateidx_mcs_mask_5ghz);

	DEBUGFS_ADD_MODE(tsf, 0600);
}

static void add_wds_files(struct ieee80211_sub_if_data *sdata)
{
	DEBUGFS_ADD(drop_unencrypted);
	DEBUGFS_ADD(flags);
	DEBUGFS_ADD(state);
	DEBUGFS_ADD(channel_type);
	DEBUGFS_ADD(rc_rateidx_mask_2ghz);
	DEBUGFS_ADD(rc_rateidx_mask_5ghz);
	DEBUGFS_ADD(rc_rateidx_mcs_mask_2ghz);
	DEBUGFS_ADD(rc_rateidx_mcs_mask_5ghz);

	DEBUGFS_ADD(peer);
}

static void add_vlan_files(struct ieee80211_sub_if_data *sdata)
{
	DEBUGFS_ADD(drop_unencrypted);
	DEBUGFS_ADD(flags);
	DEBUGFS_ADD(state);
	DEBUGFS_ADD(channel_type);
	DEBUGFS_ADD(rc_rateidx_mask_2ghz);
	DEBUGFS_ADD(rc_rateidx_mask_5ghz);
	DEBUGFS_ADD(rc_rateidx_mcs_mask_2ghz);
	DEBUGFS_ADD(rc_rateidx_mcs_mask_5ghz);
}

static void add_monitor_files(struct ieee80211_sub_if_data *sdata)
{
	DEBUGFS_ADD(flags);
	DEBUGFS_ADD(state);
	DEBUGFS_ADD(channel_type);
}

#ifdef CONFIG_MAC80211_MESH

static void add_mesh_files(struct ieee80211_sub_if_data *sdata)
{
	DEBUGFS_ADD_MODE(tsf, 0600);
}

static void add_mesh_stats(struct ieee80211_sub_if_data *sdata)
{
	struct dentry *dir = debugfs_create_dir("mesh_stats",
						sdata->debugfs.dir);
#define MESHSTATS_ADD(name)\
	debugfs_create_file(#name, 0400, dir, sdata, &name##_ops);

	MESHSTATS_ADD(fwded_mcast);
	MESHSTATS_ADD(fwded_unicast);
	MESHSTATS_ADD(fwded_frames);
	MESHSTATS_ADD(dropped_frames_ttl);
	MESHSTATS_ADD(dropped_frames_no_route);
	MESHSTATS_ADD(dropped_frames_congestion);
	MESHSTATS_ADD(estab_plinks);
#undef MESHSTATS_ADD
}

static void add_mesh_config(struct ieee80211_sub_if_data *sdata)
{
	struct dentry *dir = debugfs_create_dir("mesh_config",
						sdata->debugfs.dir);

#define MESHPARAMS_ADD(name) \
	debugfs_create_file(#name, 0600, dir, sdata, &name##_ops);

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
	MESHPARAMS_ADD(dot11MeshGateAnnouncementProtocol);
	MESHPARAMS_ADD(rssi_threshold);
#undef MESHPARAMS_ADD
}
#endif

static void add_files(struct ieee80211_sub_if_data *sdata)
{
	if (!sdata->debugfs.dir)
		return;

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
	case NL80211_IFTYPE_WDS:
		add_wds_files(sdata);
		break;
	case NL80211_IFTYPE_MONITOR:
		add_monitor_files(sdata);
		break;
	case NL80211_IFTYPE_AP_VLAN:
		add_vlan_files(sdata);
		break;
	default:
		break;
	}
}

void ieee80211_debugfs_add_netdev(struct ieee80211_sub_if_data *sdata)
{
	char buf[10+IFNAMSIZ];

	sprintf(buf, "netdev:%s", sdata->name);
	sdata->debugfs.dir = debugfs_create_dir(buf,
		sdata->local->hw.wiphy->debugfsdir);
	if (sdata->debugfs.dir)
		sdata->debugfs.subdir_stations = debugfs_create_dir("stations",
			sdata->debugfs.dir);
	add_files(sdata);
}

void ieee80211_debugfs_remove_netdev(struct ieee80211_sub_if_data *sdata)
{
	if (!sdata->debugfs.dir)
		return;

	debugfs_remove_recursive(sdata->debugfs.dir);
	sdata->debugfs.dir = NULL;
}

void ieee80211_debugfs_rename_netdev(struct ieee80211_sub_if_data *sdata)
{
	struct dentry *dir;
	char buf[10 + IFNAMSIZ];

	dir = sdata->debugfs.dir;

	if (!dir)
		return;

	sprintf(buf, "netdev:%s", sdata->name);
	if (!debugfs_rename(dir->d_parent, dir, dir->d_parent, buf))
		printk(KERN_ERR "mac80211: debugfs: failed to rename debugfs "
		       "dir to %s\n", buf);
}
