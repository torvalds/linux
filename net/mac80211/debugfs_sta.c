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

#define STA_READ(name, buflen, field, format_string)			\
static ssize_t sta_ ##name## _read(struct file *file,			\
				   char __user *userbuf,		\
				   size_t count, loff_t *ppos)		\
{									\
	int res;							\
	struct sta_info *sta = file->private_data;			\
	char buf[buflen];						\
	res = scnprintf(buf, buflen, format_string, sta->field);	\
	return simple_read_from_buffer(userbuf, count, ppos, buf, res);	\
}
#define STA_READ_D(name, field) STA_READ(name, 20, field, "%d\n")
#define STA_READ_U(name, field) STA_READ(name, 20, field, "%u\n")
#define STA_READ_LU(name, field) STA_READ(name, 20, field, "%lu\n")
#define STA_READ_S(name, field) STA_READ(name, 20, field, "%s\n")

#define STA_OPS(name)							\
static const struct file_operations sta_ ##name## _ops = {		\
	.read = sta_##name##_read,					\
	.open = mac80211_open_file_generic,				\
}

#define STA_FILE(name, field, format)					\
		STA_READ_##format(name, field)				\
		STA_OPS(name)

STA_FILE(aid, sta.aid, D);
STA_FILE(dev, sdata->dev->name, S);
STA_FILE(rx_packets, rx_packets, LU);
STA_FILE(tx_packets, tx_packets, LU);
STA_FILE(rx_bytes, rx_bytes, LU);
STA_FILE(tx_bytes, tx_bytes, LU);
STA_FILE(rx_duplicates, num_duplicates, LU);
STA_FILE(rx_fragments, rx_fragments, LU);
STA_FILE(rx_dropped, rx_dropped, LU);
STA_FILE(tx_fragments, tx_fragments, LU);
STA_FILE(tx_filtered, tx_filtered_count, LU);
STA_FILE(tx_retry_failed, tx_retry_failed, LU);
STA_FILE(tx_retry_count, tx_retry_count, LU);
STA_FILE(last_signal, last_signal, D);
STA_FILE(last_qual, last_qual, D);
STA_FILE(last_noise, last_noise, D);
STA_FILE(wep_weak_iv_count, wep_weak_iv_count, LU);

static ssize_t sta_flags_read(struct file *file, char __user *userbuf,
			      size_t count, loff_t *ppos)
{
	char buf[100];
	struct sta_info *sta = file->private_data;
	u32 staflags = get_sta_flags(sta);
	int res = scnprintf(buf, sizeof(buf), "%s%s%s%s%s%s%s%s",
		staflags & WLAN_STA_AUTH ? "AUTH\n" : "",
		staflags & WLAN_STA_ASSOC ? "ASSOC\n" : "",
		staflags & WLAN_STA_PS ? "PS\n" : "",
		staflags & WLAN_STA_AUTHORIZED ? "AUTHORIZED\n" : "",
		staflags & WLAN_STA_SHORT_PREAMBLE ? "SHORT PREAMBLE\n" : "",
		staflags & WLAN_STA_WME ? "WME\n" : "",
		staflags & WLAN_STA_WDS ? "WDS\n" : "",
		staflags & WLAN_STA_MFP ? "MFP\n" : "");
	return simple_read_from_buffer(userbuf, count, ppos, buf, res);
}
STA_OPS(flags);

static ssize_t sta_num_ps_buf_frames_read(struct file *file,
					  char __user *userbuf,
					  size_t count, loff_t *ppos)
{
	char buf[20];
	struct sta_info *sta = file->private_data;
	int res = scnprintf(buf, sizeof(buf), "%u\n",
			    skb_queue_len(&sta->ps_tx_buf));
	return simple_read_from_buffer(userbuf, count, ppos, buf, res);
}
STA_OPS(num_ps_buf_frames);

static ssize_t sta_inactive_ms_read(struct file *file, char __user *userbuf,
				    size_t count, loff_t *ppos)
{
	char buf[20];
	struct sta_info *sta = file->private_data;
	int res = scnprintf(buf, sizeof(buf), "%d\n",
			    jiffies_to_msecs(jiffies - sta->last_rx));
	return simple_read_from_buffer(userbuf, count, ppos, buf, res);
}
STA_OPS(inactive_ms);

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
	char buf[30 + STA_TID_NUM * 70], *p = buf;
	int i;
	struct sta_info *sta = file->private_data;

	spin_lock_bh(&sta->lock);
	p += scnprintf(p, sizeof(buf)+buf-p, "next dialog_token is %#02x\n",
			sta->ampdu_mlme.dialog_token_allocator + 1);
	for (i = 0; i < STA_TID_NUM; i++) {
		p += scnprintf(p, sizeof(buf)+buf-p, "TID %02d:", i);
		p += scnprintf(p, sizeof(buf)+buf-p, " RX=%x",
				sta->ampdu_mlme.tid_state_rx[i]);
		p += scnprintf(p, sizeof(buf)+buf-p, "/DTKN=%#.2x",
				sta->ampdu_mlme.tid_state_rx[i] ?
				sta->ampdu_mlme.tid_rx[i]->dialog_token : 0);
		p += scnprintf(p, sizeof(buf)+buf-p, "/SSN=%#.3x",
				sta->ampdu_mlme.tid_state_rx[i] ?
				sta->ampdu_mlme.tid_rx[i]->ssn : 0);

		p += scnprintf(p, sizeof(buf)+buf-p, " TX=%x",
				sta->ampdu_mlme.tid_state_tx[i]);
		p += scnprintf(p, sizeof(buf)+buf-p, "/DTKN=%#.2x",
				sta->ampdu_mlme.tid_state_tx[i] ?
				sta->ampdu_mlme.tid_tx[i]->dialog_token : 0);
		p += scnprintf(p, sizeof(buf)+buf-p, "/SSN=%#.3x",
				sta->ampdu_mlme.tid_state_tx[i] ?
				sta->ampdu_mlme.tid_tx[i]->ssn : 0);
		p += scnprintf(p, sizeof(buf)+buf-p, "/pending=%03d",
				sta->ampdu_mlme.tid_state_tx[i] ?
				skb_queue_len(&sta->ampdu_mlme.tid_tx[i]->pending) : 0);
		p += scnprintf(p, sizeof(buf)+buf-p, "\n");
	}
	spin_unlock_bh(&sta->lock);

	return simple_read_from_buffer(userbuf, count, ppos, buf, p - buf);
}
STA_OPS(agg_status);

#define DEBUGFS_ADD(name) \
	sta->debugfs.name = debugfs_create_file(#name, 0400, \
		sta->debugfs.dir, sta, &sta_ ##name## _ops);

#define DEBUGFS_DEL(name) \
	debugfs_remove(sta->debugfs.name);\
	sta->debugfs.name = NULL;


void ieee80211_sta_debugfs_add(struct sta_info *sta)
{
	struct dentry *stations_dir = sta->local->debugfs.stations;
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
	DEBUGFS_ADD(last_seq_ctrl);
	DEBUGFS_ADD(agg_status);
	DEBUGFS_ADD(dev);
	DEBUGFS_ADD(rx_packets);
	DEBUGFS_ADD(tx_packets);
	DEBUGFS_ADD(rx_bytes);
	DEBUGFS_ADD(tx_bytes);
	DEBUGFS_ADD(rx_duplicates);
	DEBUGFS_ADD(rx_fragments);
	DEBUGFS_ADD(rx_dropped);
	DEBUGFS_ADD(tx_fragments);
	DEBUGFS_ADD(tx_filtered);
	DEBUGFS_ADD(tx_retry_failed);
	DEBUGFS_ADD(tx_retry_count);
	DEBUGFS_ADD(last_signal);
	DEBUGFS_ADD(last_qual);
	DEBUGFS_ADD(last_noise);
	DEBUGFS_ADD(wep_weak_iv_count);
}

void ieee80211_sta_debugfs_remove(struct sta_info *sta)
{
	DEBUGFS_DEL(flags);
	DEBUGFS_DEL(num_ps_buf_frames);
	DEBUGFS_DEL(inactive_ms);
	DEBUGFS_DEL(last_seq_ctrl);
	DEBUGFS_DEL(agg_status);
	DEBUGFS_DEL(aid);
	DEBUGFS_DEL(dev);
	DEBUGFS_DEL(rx_packets);
	DEBUGFS_DEL(tx_packets);
	DEBUGFS_DEL(rx_bytes);
	DEBUGFS_DEL(tx_bytes);
	DEBUGFS_DEL(rx_duplicates);
	DEBUGFS_DEL(rx_fragments);
	DEBUGFS_DEL(rx_dropped);
	DEBUGFS_DEL(tx_fragments);
	DEBUGFS_DEL(tx_filtered);
	DEBUGFS_DEL(tx_retry_failed);
	DEBUGFS_DEL(tx_retry_count);
	DEBUGFS_DEL(last_signal);
	DEBUGFS_DEL(last_qual);
	DEBUGFS_DEL(last_noise);
	DEBUGFS_DEL(wep_weak_iv_count);

	debugfs_remove(sta->debugfs.dir);
	sta->debugfs.dir = NULL;
}
