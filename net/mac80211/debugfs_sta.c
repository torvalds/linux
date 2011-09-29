/*
 * Copyright 2003-2005	Devicescape Software, Inc.
 * Copyright (c) 2006	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007	Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/debugfs.h>
#include <linux/ieee80211.h>
#include "ieee80211_i.h"
#include "debugfs.h"
#include "debugfs_sta.h"
#include "sta_info.h"

/* sta attributtes */

#define STA_READ(name, field, format_string)				\
static ssize_t sta_ ##name## _read(struct file *file,			\
				   char __user *userbuf,		\
				   size_t count, loff_t *ppos)		\
{									\
	struct sta_info *sta = file->private_data;			\
	return mac80211_format_buffer(userbuf, count, ppos, 		\
				      format_string, sta->field);	\
}
#define STA_READ_D(name, field) STA_READ(name, field, "%d\n")
#define STA_READ_U(name, field) STA_READ(name, field, "%u\n")
#define STA_READ_S(name, field) STA_READ(name, field, "%s\n")

#define STA_OPS(name)							\
static const struct file_operations sta_ ##name## _ops = {		\
	.read = sta_##name##_read,					\
	.open = mac80211_open_file_generic,				\
	.llseek = generic_file_llseek,					\
}

#define STA_OPS_RW(name)						\
static const struct file_operations sta_ ##name## _ops = {		\
	.read = sta_##name##_read,					\
	.write = sta_##name##_write,					\
	.open = mac80211_open_file_generic,				\
	.llseek = generic_file_llseek,					\
}

#define STA_FILE(name, field, format)					\
		STA_READ_##format(name, field)				\
		STA_OPS(name)

STA_FILE(aid, sta.aid, D);
STA_FILE(dev, sdata->name, S);
STA_FILE(last_signal, last_signal, D);

static ssize_t sta_flags_read(struct file *file, char __user *userbuf,
			      size_t count, loff_t *ppos)
{
	char buf[100];
	struct sta_info *sta = file->private_data;

	int res = scnprintf(buf, sizeof(buf), "%s%s%s%s%s%s%s%s%s",
		test_sta_flag(sta, WLAN_STA_AUTH) ? "AUTH\n" : "",
		test_sta_flag(sta, WLAN_STA_ASSOC) ? "ASSOC\n" : "",
		test_sta_flag(sta, WLAN_STA_PS_STA) ? "PS (sta)\n" : "",
		test_sta_flag(sta, WLAN_STA_PS_DRIVER) ? "PS (driver)\n" : "",
		test_sta_flag(sta, WLAN_STA_AUTHORIZED) ? "AUTHORIZED\n" : "",
		test_sta_flag(sta, WLAN_STA_SHORT_PREAMBLE) ? "SHORT PREAMBLE\n" : "",
		test_sta_flag(sta, WLAN_STA_WME) ? "WME\n" : "",
		test_sta_flag(sta, WLAN_STA_WDS) ? "WDS\n" : "",
		test_sta_flag(sta, WLAN_STA_MFP) ? "MFP\n" : "");
	return simple_read_from_buffer(userbuf, count, ppos, buf, res);
}
STA_OPS(flags);

static ssize_t sta_num_ps_buf_frames_read(struct file *file,
					  char __user *userbuf,
					  size_t count, loff_t *ppos)
{
	struct sta_info *sta = file->private_data;
	char buf[17*IEEE80211_NUM_ACS], *p = buf;
	int ac;

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++)
		p += scnprintf(p, sizeof(buf)+buf-p, "AC%d: %d\n", ac,
			       skb_queue_len(&sta->ps_tx_buf[ac]) +
			       skb_queue_len(&sta->tx_filtered[ac]));
	return simple_read_from_buffer(userbuf, count, ppos, buf, p - buf);
}
STA_OPS(num_ps_buf_frames);

static ssize_t sta_inactive_ms_read(struct file *file, char __user *userbuf,
				    size_t count, loff_t *ppos)
{
	struct sta_info *sta = file->private_data;
	return mac80211_format_buffer(userbuf, count, ppos, "%d\n",
				      jiffies_to_msecs(jiffies - sta->last_rx));
}
STA_OPS(inactive_ms);


static ssize_t sta_connected_time_read(struct file *file, char __user *userbuf,
					size_t count, loff_t *ppos)
{
	struct sta_info *sta = file->private_data;
	struct timespec uptime;
	struct tm result;
	long connected_time_secs;
	char buf[100];
	int res;
	do_posix_clock_monotonic_gettime(&uptime);
	connected_time_secs = uptime.tv_sec - sta->last_connected;
	time_to_tm(connected_time_secs, 0, &result);
	result.tm_year -= 70;
	result.tm_mday -= 1;
	res = scnprintf(buf, sizeof(buf),
		"years  - %ld\nmonths - %d\ndays   - %d\nclock  - %d:%d:%d\n\n",
			result.tm_year, result.tm_mon, result.tm_mday,
			result.tm_hour, result.tm_min, result.tm_sec);
	return simple_read_from_buffer(userbuf, count, ppos, buf, res);
}
STA_OPS(connected_time);



static ssize_t sta_last_seq_ctrl_read(struct file *file, char __user *userbuf,
				      size_t count, loff_t *ppos)
{
	char buf[15*NUM_RX_DATA_QUEUES], *p = buf;
	int i;
	struct sta_info *sta = file->private_data;
	for (i = 0; i < NUM_RX_DATA_QUEUES; i++)
		p += scnprintf(p, sizeof(buf)+buf-p, "%x ",
			       le16_to_cpu(sta->last_seq_ctrl[i]));
	p += scnprintf(p, sizeof(buf)+buf-p, "\n");
	return simple_read_from_buffer(userbuf, count, ppos, buf, p - buf);
}
STA_OPS(last_seq_ctrl);

static ssize_t sta_agg_status_read(struct file *file, char __user *userbuf,
					size_t count, loff_t *ppos)
{
	char buf[71 + STA_TID_NUM * 40], *p = buf;
	int i;
	struct sta_info *sta = file->private_data;
	struct tid_ampdu_rx *tid_rx;
	struct tid_ampdu_tx *tid_tx;

	rcu_read_lock();

	p += scnprintf(p, sizeof(buf) + buf - p, "next dialog_token: %#02x\n",
			sta->ampdu_mlme.dialog_token_allocator + 1);
	p += scnprintf(p, sizeof(buf) + buf - p,
		       "TID\t\tRX active\tDTKN\tSSN\t\tTX\tDTKN\tpending\n");

	for (i = 0; i < STA_TID_NUM; i++) {
		tid_rx = rcu_dereference(sta->ampdu_mlme.tid_rx[i]);
		tid_tx = rcu_dereference(sta->ampdu_mlme.tid_tx[i]);

		p += scnprintf(p, sizeof(buf) + buf - p, "%02d", i);
		p += scnprintf(p, sizeof(buf) + buf - p, "\t\t%x", !!tid_rx);
		p += scnprintf(p, sizeof(buf) + buf - p, "\t%#.2x",
				tid_rx ? tid_rx->dialog_token : 0);
		p += scnprintf(p, sizeof(buf) + buf - p, "\t%#.3x",
				tid_rx ? tid_rx->ssn : 0);

		p += scnprintf(p, sizeof(buf) + buf - p, "\t\t%x", !!tid_tx);
		p += scnprintf(p, sizeof(buf) + buf - p, "\t%#.2x",
				tid_tx ? tid_tx->dialog_token : 0);
		p += scnprintf(p, sizeof(buf) + buf - p, "\t%03d",
				tid_tx ? skb_queue_len(&tid_tx->pending) : 0);
		p += scnprintf(p, sizeof(buf) + buf - p, "\n");
	}
	rcu_read_unlock();

	return simple_read_from_buffer(userbuf, count, ppos, buf, p - buf);
}

static ssize_t sta_agg_status_write(struct file *file, const char __user *userbuf,
				    size_t count, loff_t *ppos)
{
	char _buf[12], *buf = _buf;
	struct sta_info *sta = file->private_data;
	bool start, tx;
	unsigned long tid;
	int ret;

	if (count > sizeof(_buf))
		return -EINVAL;

	if (copy_from_user(buf, userbuf, count))
		return -EFAULT;

	buf[sizeof(_buf) - 1] = '\0';

	if (strncmp(buf, "tx ", 3) == 0) {
		buf += 3;
		tx = true;
	} else if (strncmp(buf, "rx ", 3) == 0) {
		buf += 3;
		tx = false;
	} else
		return -EINVAL;

	if (strncmp(buf, "start ", 6) == 0) {
		buf += 6;
		start = true;
		if (!tx)
			return -EINVAL;
	} else if (strncmp(buf, "stop ", 5) == 0) {
		buf += 5;
		start = false;
	} else
		return -EINVAL;

	tid = simple_strtoul(buf, NULL, 0);

	if (tid >= STA_TID_NUM)
		return -EINVAL;

	if (tx) {
		if (start)
			ret = ieee80211_start_tx_ba_session(&sta->sta, tid, 5000);
		else
			ret = ieee80211_stop_tx_ba_session(&sta->sta, tid);
	} else {
		__ieee80211_stop_rx_ba_session(sta, tid, WLAN_BACK_RECIPIENT,
					       3, true);
		ret = 0;
	}

	return ret ?: count;
}
STA_OPS_RW(agg_status);

static ssize_t sta_ht_capa_read(struct file *file, char __user *userbuf,
				size_t count, loff_t *ppos)
{
#define PRINT_HT_CAP(_cond, _str) \
	do { \
	if (_cond) \
			p += scnprintf(p, sizeof(buf)+buf-p, "\t" _str "\n"); \
	} while (0)
	char buf[512], *p = buf;
	int i;
	struct sta_info *sta = file->private_data;
	struct ieee80211_sta_ht_cap *htc = &sta->sta.ht_cap;

	p += scnprintf(p, sizeof(buf) + buf - p, "ht %ssupported\n",
			htc->ht_supported ? "" : "not ");
	if (htc->ht_supported) {
		p += scnprintf(p, sizeof(buf)+buf-p, "cap: %#.4x\n", htc->cap);

		PRINT_HT_CAP((htc->cap & BIT(0)), "RX LDPC");
		PRINT_HT_CAP((htc->cap & BIT(1)), "HT20/HT40");
		PRINT_HT_CAP(!(htc->cap & BIT(1)), "HT20");

		PRINT_HT_CAP(((htc->cap >> 2) & 0x3) == 0, "Static SM Power Save");
		PRINT_HT_CAP(((htc->cap >> 2) & 0x3) == 1, "Dynamic SM Power Save");
		PRINT_HT_CAP(((htc->cap >> 2) & 0x3) == 3, "SM Power Save disabled");

		PRINT_HT_CAP((htc->cap & BIT(4)), "RX Greenfield");
		PRINT_HT_CAP((htc->cap & BIT(5)), "RX HT20 SGI");
		PRINT_HT_CAP((htc->cap & BIT(6)), "RX HT40 SGI");
		PRINT_HT_CAP((htc->cap & BIT(7)), "TX STBC");

		PRINT_HT_CAP(((htc->cap >> 8) & 0x3) == 0, "No RX STBC");
		PRINT_HT_CAP(((htc->cap >> 8) & 0x3) == 1, "RX STBC 1-stream");
		PRINT_HT_CAP(((htc->cap >> 8) & 0x3) == 2, "RX STBC 2-streams");
		PRINT_HT_CAP(((htc->cap >> 8) & 0x3) == 3, "RX STBC 3-streams");

		PRINT_HT_CAP((htc->cap & BIT(10)), "HT Delayed Block Ack");

		PRINT_HT_CAP((htc->cap & BIT(11)), "Max AMSDU length: "
			     "3839 bytes");
		PRINT_HT_CAP(!(htc->cap & BIT(11)), "Max AMSDU length: "
			     "7935 bytes");

		/*
		 * For beacons and probe response this would mean the BSS
		 * does or does not allow the usage of DSSS/CCK HT40.
		 * Otherwise it means the STA does or does not use
		 * DSSS/CCK HT40.
		 */
		PRINT_HT_CAP((htc->cap & BIT(12)), "DSSS/CCK HT40");
		PRINT_HT_CAP(!(htc->cap & BIT(12)), "No DSSS/CCK HT40");

		/* BIT(13) is reserved */

		PRINT_HT_CAP((htc->cap & BIT(14)), "40 MHz Intolerant");

		PRINT_HT_CAP((htc->cap & BIT(15)), "L-SIG TXOP protection");

		p += scnprintf(p, sizeof(buf)+buf-p, "ampdu factor/density: %d/%d\n",
				htc->ampdu_factor, htc->ampdu_density);
		p += scnprintf(p, sizeof(buf)+buf-p, "MCS mask:");

		for (i = 0; i < IEEE80211_HT_MCS_MASK_LEN; i++)
			p += scnprintf(p, sizeof(buf)+buf-p, " %.2x",
					htc->mcs.rx_mask[i]);
		p += scnprintf(p, sizeof(buf)+buf-p, "\n");

		/* If not set this is meaningless */
		if (le16_to_cpu(htc->mcs.rx_highest)) {
			p += scnprintf(p, sizeof(buf)+buf-p,
				       "MCS rx highest: %d Mbps\n",
				       le16_to_cpu(htc->mcs.rx_highest));
		}

		p += scnprintf(p, sizeof(buf)+buf-p, "MCS tx params: %x\n",
				htc->mcs.tx_params);
	}

	return simple_read_from_buffer(userbuf, count, ppos, buf, p - buf);
}
STA_OPS(ht_capa);

#define DEBUGFS_ADD(name) \
	debugfs_create_file(#name, 0400, \
		sta->debugfs.dir, sta, &sta_ ##name## _ops);

#define DEBUGFS_ADD_COUNTER(name, field)				\
	if (sizeof(sta->field) == sizeof(u32))				\
		debugfs_create_u32(#name, 0400, sta->debugfs.dir,	\
			(u32 *) &sta->field);				\
	else								\
		debugfs_create_u64(#name, 0400, sta->debugfs.dir,	\
			(u64 *) &sta->field);

void ieee80211_sta_debugfs_add(struct sta_info *sta)
{
	struct dentry *stations_dir = sta->sdata->debugfs.subdir_stations;
	u8 mac[3*ETH_ALEN];

	sta->debugfs.add_has_run = true;

	if (!stations_dir)
		return;

	snprintf(mac, sizeof(mac), "%pM", sta->sta.addr);

	/*
	 * This might fail due to a race condition:
	 * When mac80211 unlinks a station, the debugfs entries
	 * remain, but it is already possible to link a new
	 * station with the same address which triggers adding
	 * it to debugfs; therefore, if the old station isn't
	 * destroyed quickly enough the old station's debugfs
	 * dir might still be around.
	 */
	sta->debugfs.dir = debugfs_create_dir(mac, stations_dir);
	if (!sta->debugfs.dir)
		return;

	DEBUGFS_ADD(flags);
	DEBUGFS_ADD(num_ps_buf_frames);
	DEBUGFS_ADD(inactive_ms);
	DEBUGFS_ADD(connected_time);
	DEBUGFS_ADD(last_seq_ctrl);
	DEBUGFS_ADD(agg_status);
	DEBUGFS_ADD(dev);
	DEBUGFS_ADD(last_signal);
	DEBUGFS_ADD(ht_capa);

	DEBUGFS_ADD_COUNTER(rx_packets, rx_packets);
	DEBUGFS_ADD_COUNTER(tx_packets, tx_packets);
	DEBUGFS_ADD_COUNTER(rx_bytes, rx_bytes);
	DEBUGFS_ADD_COUNTER(tx_bytes, tx_bytes);
	DEBUGFS_ADD_COUNTER(rx_duplicates, num_duplicates);
	DEBUGFS_ADD_COUNTER(rx_fragments, rx_fragments);
	DEBUGFS_ADD_COUNTER(rx_dropped, rx_dropped);
	DEBUGFS_ADD_COUNTER(tx_fragments, tx_fragments);
	DEBUGFS_ADD_COUNTER(tx_filtered, tx_filtered_count);
	DEBUGFS_ADD_COUNTER(tx_retry_failed, tx_retry_failed);
	DEBUGFS_ADD_COUNTER(tx_retry_count, tx_retry_count);
	DEBUGFS_ADD_COUNTER(wep_weak_iv_count, wep_weak_iv_count);
}

void ieee80211_sta_debugfs_remove(struct sta_info *sta)
{
	debugfs_remove_recursive(sta->debugfs.dir);
	sta->debugfs.dir = NULL;
}
