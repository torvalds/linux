/*
 * Copyright 2003-2005	Devicescape Software, Inc.
 * Copyright (c) 2006	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007	Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kobject.h>
#include "ieee80211_i.h"
#include "ieee80211_key.h"
#include "debugfs.h"
#include "debugfs_key.h"

#define KEY_READ(name, buflen, format_string)				\
static ssize_t key_##name##_read(struct file *file,			\
				 char __user *userbuf,			\
				 size_t count, loff_t *ppos)		\
{									\
	char buf[buflen];						\
	struct ieee80211_key *key = file->private_data;			\
	int res = scnprintf(buf, buflen, format_string, key->name);	\
	return simple_read_from_buffer(userbuf, count, ppos, buf, res);	\
}
#define KEY_READ_D(name) KEY_READ(name, 20, "%d\n")

#define KEY_OPS(name)							\
static const struct file_operations key_ ##name## _ops = {		\
	.read = key_##name##_read,					\
	.open = mac80211_open_file_generic,				\
}

#define KEY_FILE(name, format)						\
		 KEY_READ_##format(name)				\
		 KEY_OPS(name)

KEY_FILE(keylen, D);
KEY_FILE(force_sw_encrypt, D);
KEY_FILE(keyidx, D);
KEY_FILE(hw_key_idx, D);
KEY_FILE(tx_rx_count, D);

static ssize_t key_algorithm_read(struct file *file,
				  char __user *userbuf,
				  size_t count, loff_t *ppos)
{
	char *alg;
	struct ieee80211_key *key = file->private_data;

	switch (key->alg) {
	case ALG_WEP:
		alg = "WEP\n";
		break;
	case ALG_TKIP:
		alg = "TKIP\n";
		break;
	case ALG_CCMP:
		alg = "CCMP\n";
		break;
	default:
		return 0;
	}
	return simple_read_from_buffer(userbuf, count, ppos, alg, strlen(alg));
}
KEY_OPS(algorithm);

static ssize_t key_tx_spec_read(struct file *file, char __user *userbuf,
				size_t count, loff_t *ppos)
{
	const u8 *tpn;
	char buf[20];
	int len;
	struct ieee80211_key *key = file->private_data;

	switch (key->alg) {
	case ALG_WEP:
		len = scnprintf(buf, sizeof(buf), "\n");
	case ALG_TKIP:
		len = scnprintf(buf, sizeof(buf), "%08x %04x\n",
				key->u.tkip.iv32,
				key->u.tkip.iv16);
	case ALG_CCMP:
		tpn = key->u.ccmp.tx_pn;
		len = scnprintf(buf, sizeof(buf), "%02x%02x%02x%02x%02x%02x\n",
				tpn[0], tpn[1], tpn[2], tpn[3], tpn[4], tpn[5]);
	default:
		return 0;
	}
	return simple_read_from_buffer(userbuf, count, ppos, buf, len);
}
KEY_OPS(tx_spec);

static ssize_t key_rx_spec_read(struct file *file, char __user *userbuf,
				size_t count, loff_t *ppos)
{
	struct ieee80211_key *key = file->private_data;
	char buf[14*NUM_RX_DATA_QUEUES+1], *p = buf;
	int i, len;
	const u8 *rpn;

	switch (key->alg) {
	case ALG_WEP:
		len = scnprintf(buf, sizeof(buf), "\n");
	case ALG_TKIP:
		for (i = 0; i < NUM_RX_DATA_QUEUES; i++)
			p += scnprintf(p, sizeof(buf)+buf-p,
				       "%08x %04x\n",
				       key->u.tkip.iv32_rx[i],
				       key->u.tkip.iv16_rx[i]);
		len = p - buf;
	case ALG_CCMP:
		for (i = 0; i < NUM_RX_DATA_QUEUES; i++) {
			rpn = key->u.ccmp.rx_pn[i];
			p += scnprintf(p, sizeof(buf)+buf-p,
				       "%02x%02x%02x%02x%02x%02x\n",
				       rpn[0], rpn[1], rpn[2],
				       rpn[3], rpn[4], rpn[5]);
		}
		len = p - buf;
	default:
		return 0;
	}
	return simple_read_from_buffer(userbuf, count, ppos, buf, len);
}
KEY_OPS(rx_spec);

static ssize_t key_replays_read(struct file *file, char __user *userbuf,
				size_t count, loff_t *ppos)
{
	struct ieee80211_key *key = file->private_data;
	char buf[20];
	int len;

	if (key->alg != ALG_CCMP)
		return 0;
	len = scnprintf(buf, sizeof(buf), "%u\n", key->u.ccmp.replays);
	return simple_read_from_buffer(userbuf, count, ppos, buf, len);
}
KEY_OPS(replays);

static ssize_t key_key_read(struct file *file, char __user *userbuf,
			    size_t count, loff_t *ppos)
{
	struct ieee80211_key *key = file->private_data;
	int i, res, bufsize = 2*key->keylen+2;
	char *buf = kmalloc(bufsize, GFP_KERNEL);
	char *p = buf;

	for (i = 0; i < key->keylen; i++)
		p += scnprintf(p, bufsize+buf-p, "%02x", key->key[i]);
	p += scnprintf(p, bufsize+buf-p, "\n");
	res = simple_read_from_buffer(userbuf, count, ppos, buf, p - buf);
	kfree(buf);
	return res;
}
KEY_OPS(key);

#define DEBUGFS_ADD(name) \
	key->debugfs.name = debugfs_create_file(#name, 0400,\
				key->debugfs.dir, key, &key_##name##_ops);

void ieee80211_debugfs_key_add(struct ieee80211_local *local,
			       struct ieee80211_key *key)
{
	char buf[20];

	if (!local->debugfs.keys)
		return;

	sprintf(buf, "%d", key->keyidx);
	key->debugfs.dir = debugfs_create_dir(buf,
					local->debugfs.keys);

	if (!key->debugfs.dir)
		return;

	DEBUGFS_ADD(keylen);
	DEBUGFS_ADD(force_sw_encrypt);
	DEBUGFS_ADD(keyidx);
	DEBUGFS_ADD(hw_key_idx);
	DEBUGFS_ADD(tx_rx_count);
	DEBUGFS_ADD(algorithm);
	DEBUGFS_ADD(tx_spec);
	DEBUGFS_ADD(rx_spec);
	DEBUGFS_ADD(replays);
	DEBUGFS_ADD(key);
};

#define DEBUGFS_DEL(name) \
	debugfs_remove(key->debugfs.name); key->debugfs.name = NULL;

void ieee80211_debugfs_key_remove(struct ieee80211_key *key)
{
	if (!key)
		return;

	DEBUGFS_DEL(keylen);
	DEBUGFS_DEL(force_sw_encrypt);
	DEBUGFS_DEL(keyidx);
	DEBUGFS_DEL(hw_key_idx);
	DEBUGFS_DEL(tx_rx_count);
	DEBUGFS_DEL(algorithm);
	DEBUGFS_DEL(tx_spec);
	DEBUGFS_DEL(rx_spec);
	DEBUGFS_DEL(replays);
	DEBUGFS_DEL(key);

	debugfs_remove(key->debugfs.stalink);
	key->debugfs.stalink = NULL;
	debugfs_remove(key->debugfs.dir);
	key->debugfs.dir = NULL;
}
void ieee80211_debugfs_key_add_default(struct ieee80211_sub_if_data *sdata)
{
	char buf[50];

	if (!sdata->debugfsdir)
		return;

	sprintf(buf, "../keys/%d", sdata->default_key->keyidx);
	sdata->debugfs.default_key =
		debugfs_create_symlink("default_key", sdata->debugfsdir, buf);
}
void ieee80211_debugfs_key_remove_default(struct ieee80211_sub_if_data *sdata)
{
	if (!sdata)
		return;

	debugfs_remove(sdata->debugfs.default_key);
	sdata->debugfs.default_key = NULL;
}
void ieee80211_debugfs_key_sta_link(struct ieee80211_key *key,
				    struct sta_info *sta)
{
	char buf[50];

	if (!key->debugfs.dir)
		return;

	sprintf(buf, "../sta/" MAC_FMT, MAC_ARG(sta->addr));
	key->debugfs.stalink =
		debugfs_create_symlink("station", key->debugfs.dir, buf);
}

void ieee80211_debugfs_key_sta_del(struct ieee80211_key *key,
				   struct sta_info *sta)
{
	debugfs_remove(key->debugfs.stalink);
	key->debugfs.stalink = NULL;
}
