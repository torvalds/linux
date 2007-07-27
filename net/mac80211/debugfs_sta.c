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

#define STA_READ_RATE(name, field)					\
static ssize_t sta_##name##_read(struct file *file,			\
				 char __user *userbuf,			\
				 size_t count, loff_t *ppos)		\
{									\
	struct sta_info *sta = file->private_data;			\
	struct ieee80211_local *local = wdev_priv(sta->dev->ieee80211_ptr);\
	struct ieee80211_hw_mode *mode = local->oper_hw_mode;		\
	char buf[20];							\
	int res = scnprintf(buf, sizeof(buf), "%d\n",			\
			    (sta->field >= 0 &&				\
			    sta->field < mode->num_rates) ?		\
			    mode->rates[sta->field].rate : -1);		\
	return simple_read_from_buffer(userbuf, count, ppos, buf, res);	\
}

#define STA_OPS(name)							\
static const struct file_operations sta_ ##name## _ops = {		\
	.read = sta_##name##_read,					\
	.open = mac80211_open_file_generic,				\
}

#define STA_FILE(name, field, format)					\
		STA_READ_##format(name, field)				\
		STA_OPS(name)

STA_FILE(aid, aid, D);
STA_FILE(key_idx_compression, key_idx_compression, D);
STA_FILE(dev, dev->name, S);
STA_FILE(vlan_id, vlan_id, D);
STA_FILE(rx_packets, rx_packets, LU);
STA_FILE(tx_packets, tx_packets, LU);
STA_FILE(rx_bytes, rx_bytes, LU);
STA_FILE(tx_bytes, tx_bytes, LU);
STA_FILE(rx_duplicates, num_duplicates, LU);
STA_FILE(rx_fragments, rx_fragments, LU);
STA_FILE(rx_dropped, rx_dropped, LU);
STA_FILE(tx_fragments, tx_fragments, LU);
STA_FILE(tx_filtered, tx_filtered_count, LU);
STA_FILE(txrate, txrate, RATE);
STA_FILE(last_txrate, last_txrate, RATE);
STA_FILE(tx_retry_failed, tx_retry_failed, LU);
STA_FILE(tx_retry_count, tx_retry_count, LU);
STA_FILE(last_rssi, last_rssi, D);
STA_FILE(last_signal, last_signal, D);
STA_FILE(last_noise, last_noise, D);
STA_FILE(channel_use, channel_use, D);
STA_FILE(wep_weak_iv_count, wep_weak_iv_count, D);

static ssize_t sta_flags_read(struct file *file, char __user *userbuf,
			      size_t count, loff_t *ppos)
{
	char buf[100];
	struct sta_info *sta = file->private_data;
	int res = scnprintf(buf, sizeof(buf), "%s%s%s%s%s%s%s%s%s",
		sta->flags & WLAN_STA_AUTH ? "AUTH\n" : "",
		sta->flags & WLAN_STA_ASSOC ? "ASSOC\n" : "",
		sta->flags & WLAN_STA_PS ? "PS\n" : "",
		sta->flags & WLAN_STA_TIM ? "TIM\n" : "",
		sta->flags & WLAN_STA_PERM ? "PERM\n" : "",
		sta->flags & WLAN_STA_AUTHORIZED ? "AUTHORIZED\n" : "",
		sta->flags & WLAN_STA_SHORT_PREAMBLE ? "SHORT PREAMBLE\n" : "",
		sta->flags & WLAN_STA_WME ? "WME\n" : "",
		sta->flags & WLAN_STA_WDS ? "WDS\n" : "");
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

static ssize_t sta_last_ack_rssi_read(struct file *file, char __user *userbuf,
				      size_t count, loff_t *ppos)
{
	char buf[100];
	struct sta_info *sta = file->private_data;
	int res = scnprintf(buf, sizeof(buf), "%d %d %d\n",
			    sta->last_ack_rssi[0],
			    sta->last_ack_rssi[1],
			    sta->last_ack_rssi[2]);
	return simple_read_from_buffer(userbuf, count, ppos, buf, res);
}
STA_OPS(last_ack_rssi);

static ssize_t sta_last_ack_ms_read(struct file *file, char __user *userbuf,
				    size_t count, loff_t *ppos)
{
	char buf[20];
	struct sta_info *sta = file->private_data;
	int res = scnprintf(buf, sizeof(buf), "%d\n",
			    sta->last_ack ?
			    jiffies_to_msecs(jiffies - sta->last_ack) : -1);
	return simple_read_from_buffer(userbuf, count, ppos, buf, res);
}
STA_OPS(last_ack_ms);

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

#ifdef CONFIG_MAC80211_DEBUG_COUNTERS
static ssize_t sta_wme_rx_queue_read(struct file *file, char __user *userbuf,
				     size_t count, loff_t *ppos)
{
	char buf[15*NUM_RX_DATA_QUEUES], *p = buf;
	int i;
	struct sta_info *sta = file->private_data;
	for (i = 0; i < NUM_RX_DATA_QUEUES; i++)
		p += scnprintf(p, sizeof(buf)+buf-p, "%u ",
			       sta->wme_rx_queue[i]);
	p += scnprintf(p, sizeof(buf)+buf-p, "\n");
	return simple_read_from_buffer(userbuf, count, ppos, buf, p - buf);
}
STA_OPS(wme_rx_queue);

static ssize_t sta_wme_tx_queue_read(struct file *file, char __user *userbuf,
				     size_t count, loff_t *ppos)
{
	char buf[15*NUM_TX_DATA_QUEUES], *p = buf;
	int i;
	struct sta_info *sta = file->private_data;
	for (i = 0; i < NUM_TX_DATA_QUEUES; i++)
		p += scnprintf(p, sizeof(buf)+buf-p, "%u ",
			       sta->wme_tx_queue[i]);
	p += scnprintf(p, sizeof(buf)+buf-p, "\n");
	return simple_read_from_buffer(userbuf, count, ppos, buf, p - buf);
}
STA_OPS(wme_tx_queue);
#endif

#define DEBUGFS_ADD(name) \
	sta->debugfs.name = debugfs_create_file(#name, 0444, \
		sta->debugfs.dir, sta, &sta_ ##name## _ops);

#define DEBUGFS_DEL(name) \
	debugfs_remove(sta->debugfs.name);\
	sta->debugfs.name = NULL;


void ieee80211_sta_debugfs_add(struct sta_info *sta)
{
	char buf[3*6];
	struct dentry *stations_dir = sta->local->debugfs.stations;

	if (!stations_dir)
		return;

	sprintf(buf, MAC_FMT, MAC_ARG(sta->addr));

	sta->debugfs.dir = debugfs_create_dir(buf, stations_dir);
	if (!sta->debugfs.dir)
		return;

	DEBUGFS_ADD(flags);
	DEBUGFS_ADD(num_ps_buf_frames);
	DEBUGFS_ADD(last_ack_rssi);
	DEBUGFS_ADD(last_ack_ms);
	DEBUGFS_ADD(inactive_ms);
	DEBUGFS_ADD(last_seq_ctrl);
#ifdef CONFIG_MAC80211_DEBUG_COUNTERS
	DEBUGFS_ADD(wme_rx_queue);
	DEBUGFS_ADD(wme_tx_queue);
#endif
}

void ieee80211_sta_debugfs_remove(struct sta_info *sta)
{
	DEBUGFS_DEL(flags);
	DEBUGFS_DEL(num_ps_buf_frames);
	DEBUGFS_DEL(last_ack_rssi);
	DEBUGFS_DEL(last_ack_ms);
	DEBUGFS_DEL(inactive_ms);
	DEBUGFS_DEL(last_seq_ctrl);
#ifdef CONFIG_MAC80211_DEBUG_COUNTERS
	DEBUGFS_DEL(wme_rx_queue);
	DEBUGFS_DEL(wme_tx_queue);
#endif

	debugfs_remove(sta->debugfs.dir);
	sta->debugfs.dir = NULL;
}
