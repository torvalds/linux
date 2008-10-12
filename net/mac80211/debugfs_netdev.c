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
#include <linux/notifier.h>
#include <net/mac80211.h>
#include <net/cfg80211.h>
#include "ieee80211_i.h"
#include "rate.h"
#include "debugfs.h"
#include "debugfs_netdev.h"

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

	if (ret != -EINVAL)
		ret = simple_read_from_buffer(userbuf, count, ppos, buf, ret);

	return ret;
}

#ifdef CONFIG_MAC80211_MESH
static ssize_t ieee80211_if_write(
	struct ieee80211_sub_if_data *sdata,
	char const __user *userbuf,
	size_t count, loff_t *ppos,
	int (*format)(struct ieee80211_sub_if_data *, char *))
{
	char buf[10];
	int buf_size;

	memset(buf, 0x00, sizeof(buf));
	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, userbuf, buf_size))
		return count;
	read_lock(&dev_base_lock);
	if (sdata->dev->reg_state == NETREG_REGISTERED)
		(*format)(sdata, buf);
	read_unlock(&dev_base_lock);

	return count;
}
#endif

#define IEEE80211_IF_FMT(name, field, format_string)			\
static ssize_t ieee80211_if_fmt_##name(					\
	const struct ieee80211_sub_if_data *sdata, char *buf,		\
	int buflen)							\
{									\
	return scnprintf(buf, buflen, format_string, sdata->field);	\
}
#define IEEE80211_IF_WFMT(name, field, type)				\
static int ieee80211_if_wfmt_##name(					\
	struct ieee80211_sub_if_data *sdata, char *buf)			\
{									\
	unsigned long tmp;						\
	char *endp;							\
									\
	tmp = simple_strtoul(buf, &endp, 0);				\
	if ((endp == buf) || ((type)tmp != tmp))			\
		return -EINVAL;						\
	sdata->field = tmp;						\
	return 0;							\
}
#define IEEE80211_IF_FMT_DEC(name, field)				\
		IEEE80211_IF_FMT(name, field, "%d\n")
#define IEEE80211_IF_FMT_HEX(name, field)				\
		IEEE80211_IF_FMT(name, field, "%#x\n")
#define IEEE80211_IF_FMT_SIZE(name, field)				\
		IEEE80211_IF_FMT(name, field, "%zd\n")

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
	DECLARE_MAC_BUF(mac);						\
	return scnprintf(buf, buflen, "%s\n", print_mac(mac, sdata->field));\
}

#define __IEEE80211_IF_FILE(name)					\
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
	.open = mac80211_open_file_generic,				\
}

#define IEEE80211_IF_FILE(name, field, format)				\
		IEEE80211_IF_FMT_##format(name, field)			\
		__IEEE80211_IF_FILE(name)

#define __IEEE80211_IF_WFILE(name)					\
static ssize_t ieee80211_if_read_##name(struct file *file,		\
					char __user *userbuf,		\
					size_t count, loff_t *ppos)	\
{									\
	return ieee80211_if_read(file->private_data,			\
				 userbuf, count, ppos,			\
				 ieee80211_if_fmt_##name);		\
}									\
static ssize_t ieee80211_if_write_##name(struct file *file,		\
					const char __user *userbuf,	\
					size_t count, loff_t *ppos)	\
{									\
	return ieee80211_if_write(file->private_data,			\
				 userbuf, count, ppos,			\
				 ieee80211_if_wfmt_##name);		\
}									\
static const struct file_operations name##_ops = {			\
	.read = ieee80211_if_read_##name,				\
	.write = ieee80211_if_write_##name,				\
	.open = mac80211_open_file_generic,				\
}

#define IEEE80211_IF_WFILE(name, field, format, type)			\
		IEEE80211_IF_FMT_##format(name, field)			\
		IEEE80211_IF_WFMT(name, field, type)			\
		__IEEE80211_IF_WFILE(name)

/* common attributes */
IEEE80211_IF_FILE(drop_unencrypted, drop_unencrypted, DEC);
IEEE80211_IF_FILE(force_unicast_rateidx, force_unicast_rateidx, DEC);
IEEE80211_IF_FILE(max_ratectrl_rateidx, max_ratectrl_rateidx, DEC);

/* STA/IBSS attributes */
IEEE80211_IF_FILE(state, u.sta.state, DEC);
IEEE80211_IF_FILE(bssid, u.sta.bssid, MAC);
IEEE80211_IF_FILE(prev_bssid, u.sta.prev_bssid, MAC);
IEEE80211_IF_FILE(ssid_len, u.sta.ssid_len, SIZE);
IEEE80211_IF_FILE(aid, u.sta.aid, DEC);
IEEE80211_IF_FILE(ap_capab, u.sta.ap_capab, HEX);
IEEE80211_IF_FILE(capab, u.sta.capab, HEX);
IEEE80211_IF_FILE(extra_ie_len, u.sta.extra_ie_len, SIZE);
IEEE80211_IF_FILE(auth_tries, u.sta.auth_tries, DEC);
IEEE80211_IF_FILE(assoc_tries, u.sta.assoc_tries, DEC);
IEEE80211_IF_FILE(auth_algs, u.sta.auth_algs, HEX);
IEEE80211_IF_FILE(auth_alg, u.sta.auth_alg, DEC);
IEEE80211_IF_FILE(auth_transaction, u.sta.auth_transaction, DEC);

static ssize_t ieee80211_if_fmt_flags(
	const struct ieee80211_sub_if_data *sdata, char *buf, int buflen)
{
	return scnprintf(buf, buflen, "%s%s%s%s%s%s%s\n",
		 sdata->u.sta.flags & IEEE80211_STA_SSID_SET ? "SSID\n" : "",
		 sdata->u.sta.flags & IEEE80211_STA_BSSID_SET ? "BSSID\n" : "",
		 sdata->u.sta.flags & IEEE80211_STA_PREV_BSSID_SET ? "prev BSSID\n" : "",
		 sdata->u.sta.flags & IEEE80211_STA_AUTHENTICATED ? "AUTH\n" : "",
		 sdata->u.sta.flags & IEEE80211_STA_ASSOCIATED ? "ASSOC\n" : "",
		 sdata->u.sta.flags & IEEE80211_STA_PROBEREQ_POLL ? "PROBEREQ POLL\n" : "",
		 sdata->bss_conf.use_cts_prot ? "CTS prot\n" : "");
}
__IEEE80211_IF_FILE(flags);

/* AP attributes */
IEEE80211_IF_FILE(num_sta_ps, u.ap.num_sta_ps, ATOMIC);
IEEE80211_IF_FILE(dtim_count, u.ap.dtim_count, DEC);

static ssize_t ieee80211_if_fmt_num_buffered_multicast(
	const struct ieee80211_sub_if_data *sdata, char *buf, int buflen)
{
	return scnprintf(buf, buflen, "%u\n",
			 skb_queue_len(&sdata->u.ap.ps_bc_buf));
}
__IEEE80211_IF_FILE(num_buffered_multicast);

/* WDS attributes */
IEEE80211_IF_FILE(peer, u.wds.remote_addr, MAC);

#ifdef CONFIG_MAC80211_MESH
/* Mesh stats attributes */
IEEE80211_IF_FILE(fwded_frames, u.mesh.mshstats.fwded_frames, DEC);
IEEE80211_IF_FILE(dropped_frames_ttl, u.mesh.mshstats.dropped_frames_ttl, DEC);
IEEE80211_IF_FILE(dropped_frames_no_route,
		u.mesh.mshstats.dropped_frames_no_route, DEC);
IEEE80211_IF_FILE(estab_plinks, u.mesh.mshstats.estab_plinks, ATOMIC);

/* Mesh parameters */
IEEE80211_IF_WFILE(dot11MeshMaxRetries,
		u.mesh.mshcfg.dot11MeshMaxRetries, DEC, u8);
IEEE80211_IF_WFILE(dot11MeshRetryTimeout,
		u.mesh.mshcfg.dot11MeshRetryTimeout, DEC, u16);
IEEE80211_IF_WFILE(dot11MeshConfirmTimeout,
		u.mesh.mshcfg.dot11MeshConfirmTimeout, DEC, u16);
IEEE80211_IF_WFILE(dot11MeshHoldingTimeout,
		u.mesh.mshcfg.dot11MeshHoldingTimeout, DEC, u16);
IEEE80211_IF_WFILE(dot11MeshTTL, u.mesh.mshcfg.dot11MeshTTL, DEC, u8);
IEEE80211_IF_WFILE(auto_open_plinks, u.mesh.mshcfg.auto_open_plinks, DEC, u8);
IEEE80211_IF_WFILE(dot11MeshMaxPeerLinks,
		u.mesh.mshcfg.dot11MeshMaxPeerLinks, DEC, u16);
IEEE80211_IF_WFILE(dot11MeshHWMPactivePathTimeout,
		u.mesh.mshcfg.dot11MeshHWMPactivePathTimeout, DEC, u32);
IEEE80211_IF_WFILE(dot11MeshHWMPpreqMinInterval,
		u.mesh.mshcfg.dot11MeshHWMPpreqMinInterval, DEC, u16);
IEEE80211_IF_WFILE(dot11MeshHWMPnetDiameterTraversalTime,
		u.mesh.mshcfg.dot11MeshHWMPnetDiameterTraversalTime, DEC, u16);
IEEE80211_IF_WFILE(dot11MeshHWMPmaxPREQretries,
		u.mesh.mshcfg.dot11MeshHWMPmaxPREQretries, DEC, u8);
IEEE80211_IF_WFILE(path_refresh_time,
		u.mesh.mshcfg.path_refresh_time, DEC, u32);
IEEE80211_IF_WFILE(min_discovery_timeout,
		u.mesh.mshcfg.min_discovery_timeout, DEC, u16);
#endif


#define DEBUGFS_ADD(name, type)\
	sdata->debugfs.type.name = debugfs_create_file(#name, 0400,\
		sdata->debugfsdir, sdata, &name##_ops);

static void add_sta_files(struct ieee80211_sub_if_data *sdata)
{
	DEBUGFS_ADD(drop_unencrypted, sta);
	DEBUGFS_ADD(force_unicast_rateidx, sta);
	DEBUGFS_ADD(max_ratectrl_rateidx, sta);

	DEBUGFS_ADD(state, sta);
	DEBUGFS_ADD(bssid, sta);
	DEBUGFS_ADD(prev_bssid, sta);
	DEBUGFS_ADD(ssid_len, sta);
	DEBUGFS_ADD(aid, sta);
	DEBUGFS_ADD(ap_capab, sta);
	DEBUGFS_ADD(capab, sta);
	DEBUGFS_ADD(extra_ie_len, sta);
	DEBUGFS_ADD(auth_tries, sta);
	DEBUGFS_ADD(assoc_tries, sta);
	DEBUGFS_ADD(auth_algs, sta);
	DEBUGFS_ADD(auth_alg, sta);
	DEBUGFS_ADD(auth_transaction, sta);
	DEBUGFS_ADD(flags, sta);
}

static void add_ap_files(struct ieee80211_sub_if_data *sdata)
{
	DEBUGFS_ADD(drop_unencrypted, ap);
	DEBUGFS_ADD(force_unicast_rateidx, ap);
	DEBUGFS_ADD(max_ratectrl_rateidx, ap);

	DEBUGFS_ADD(num_sta_ps, ap);
	DEBUGFS_ADD(dtim_count, ap);
	DEBUGFS_ADD(num_buffered_multicast, ap);
}

static void add_wds_files(struct ieee80211_sub_if_data *sdata)
{
	DEBUGFS_ADD(drop_unencrypted, wds);
	DEBUGFS_ADD(force_unicast_rateidx, wds);
	DEBUGFS_ADD(max_ratectrl_rateidx, wds);

	DEBUGFS_ADD(peer, wds);
}

static void add_vlan_files(struct ieee80211_sub_if_data *sdata)
{
	DEBUGFS_ADD(drop_unencrypted, vlan);
	DEBUGFS_ADD(force_unicast_rateidx, vlan);
	DEBUGFS_ADD(max_ratectrl_rateidx, vlan);
}

static void add_monitor_files(struct ieee80211_sub_if_data *sdata)
{
}

#ifdef CONFIG_MAC80211_MESH
#define MESHSTATS_ADD(name)\
	sdata->mesh_stats.name = debugfs_create_file(#name, 0400,\
		sdata->mesh_stats_dir, sdata, &name##_ops);

static void add_mesh_stats(struct ieee80211_sub_if_data *sdata)
{
	sdata->mesh_stats_dir = debugfs_create_dir("mesh_stats",
				sdata->debugfsdir);
	MESHSTATS_ADD(fwded_frames);
	MESHSTATS_ADD(dropped_frames_ttl);
	MESHSTATS_ADD(dropped_frames_no_route);
	MESHSTATS_ADD(estab_plinks);
}

#define MESHPARAMS_ADD(name)\
	sdata->mesh_config.name = debugfs_create_file(#name, 0600,\
		sdata->mesh_config_dir, sdata, &name##_ops);

static void add_mesh_config(struct ieee80211_sub_if_data *sdata)
{
	sdata->mesh_config_dir = debugfs_create_dir("mesh_config",
				sdata->debugfsdir);
	MESHPARAMS_ADD(dot11MeshMaxRetries);
	MESHPARAMS_ADD(dot11MeshRetryTimeout);
	MESHPARAMS_ADD(dot11MeshConfirmTimeout);
	MESHPARAMS_ADD(dot11MeshHoldingTimeout);
	MESHPARAMS_ADD(dot11MeshTTL);
	MESHPARAMS_ADD(auto_open_plinks);
	MESHPARAMS_ADD(dot11MeshMaxPeerLinks);
	MESHPARAMS_ADD(dot11MeshHWMPactivePathTimeout);
	MESHPARAMS_ADD(dot11MeshHWMPpreqMinInterval);
	MESHPARAMS_ADD(dot11MeshHWMPnetDiameterTraversalTime);
	MESHPARAMS_ADD(dot11MeshHWMPmaxPREQretries);
	MESHPARAMS_ADD(path_refresh_time);
	MESHPARAMS_ADD(min_discovery_timeout);
}
#endif

static void add_files(struct ieee80211_sub_if_data *sdata)
{
	if (!sdata->debugfsdir)
		return;

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_MESH_POINT:
#ifdef CONFIG_MAC80211_MESH
		add_mesh_stats(sdata);
		add_mesh_config(sdata);
#endif
		break;
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_ADHOC:
		add_sta_files(sdata);
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

#define DEBUGFS_DEL(name, type)					\
	do {							\
		debugfs_remove(sdata->debugfs.type.name);	\
		sdata->debugfs.type.name = NULL;		\
	} while (0)

static void del_sta_files(struct ieee80211_sub_if_data *sdata)
{
	DEBUGFS_DEL(drop_unencrypted, sta);
	DEBUGFS_DEL(force_unicast_rateidx, sta);
	DEBUGFS_DEL(max_ratectrl_rateidx, sta);

	DEBUGFS_DEL(state, sta);
	DEBUGFS_DEL(bssid, sta);
	DEBUGFS_DEL(prev_bssid, sta);
	DEBUGFS_DEL(ssid_len, sta);
	DEBUGFS_DEL(aid, sta);
	DEBUGFS_DEL(ap_capab, sta);
	DEBUGFS_DEL(capab, sta);
	DEBUGFS_DEL(extra_ie_len, sta);
	DEBUGFS_DEL(auth_tries, sta);
	DEBUGFS_DEL(assoc_tries, sta);
	DEBUGFS_DEL(auth_algs, sta);
	DEBUGFS_DEL(auth_alg, sta);
	DEBUGFS_DEL(auth_transaction, sta);
	DEBUGFS_DEL(flags, sta);
}

static void del_ap_files(struct ieee80211_sub_if_data *sdata)
{
	DEBUGFS_DEL(drop_unencrypted, ap);
	DEBUGFS_DEL(force_unicast_rateidx, ap);
	DEBUGFS_DEL(max_ratectrl_rateidx, ap);

	DEBUGFS_DEL(num_sta_ps, ap);
	DEBUGFS_DEL(dtim_count, ap);
	DEBUGFS_DEL(num_buffered_multicast, ap);
}

static void del_wds_files(struct ieee80211_sub_if_data *sdata)
{
	DEBUGFS_DEL(drop_unencrypted, wds);
	DEBUGFS_DEL(force_unicast_rateidx, wds);
	DEBUGFS_DEL(max_ratectrl_rateidx, wds);

	DEBUGFS_DEL(peer, wds);
}

static void del_vlan_files(struct ieee80211_sub_if_data *sdata)
{
	DEBUGFS_DEL(drop_unencrypted, vlan);
	DEBUGFS_DEL(force_unicast_rateidx, vlan);
	DEBUGFS_DEL(max_ratectrl_rateidx, vlan);
}

static void del_monitor_files(struct ieee80211_sub_if_data *sdata)
{
}

#ifdef CONFIG_MAC80211_MESH
#define MESHSTATS_DEL(name)			\
	do {						\
		debugfs_remove(sdata->mesh_stats.name);	\
		sdata->mesh_stats.name = NULL;		\
	} while (0)

static void del_mesh_stats(struct ieee80211_sub_if_data *sdata)
{
	MESHSTATS_DEL(fwded_frames);
	MESHSTATS_DEL(dropped_frames_ttl);
	MESHSTATS_DEL(dropped_frames_no_route);
	MESHSTATS_DEL(estab_plinks);
	debugfs_remove(sdata->mesh_stats_dir);
	sdata->mesh_stats_dir = NULL;
}

#define MESHPARAMS_DEL(name)			\
	do {						\
		debugfs_remove(sdata->mesh_config.name);	\
		sdata->mesh_config.name = NULL;		\
	} while (0)

static void del_mesh_config(struct ieee80211_sub_if_data *sdata)
{
	MESHPARAMS_DEL(dot11MeshMaxRetries);
	MESHPARAMS_DEL(dot11MeshRetryTimeout);
	MESHPARAMS_DEL(dot11MeshConfirmTimeout);
	MESHPARAMS_DEL(dot11MeshHoldingTimeout);
	MESHPARAMS_DEL(dot11MeshTTL);
	MESHPARAMS_DEL(auto_open_plinks);
	MESHPARAMS_DEL(dot11MeshMaxPeerLinks);
	MESHPARAMS_DEL(dot11MeshHWMPactivePathTimeout);
	MESHPARAMS_DEL(dot11MeshHWMPpreqMinInterval);
	MESHPARAMS_DEL(dot11MeshHWMPnetDiameterTraversalTime);
	MESHPARAMS_DEL(dot11MeshHWMPmaxPREQretries);
	MESHPARAMS_DEL(path_refresh_time);
	MESHPARAMS_DEL(min_discovery_timeout);
	debugfs_remove(sdata->mesh_config_dir);
	sdata->mesh_config_dir = NULL;
}
#endif

static void del_files(struct ieee80211_sub_if_data *sdata)
{
	if (!sdata->debugfsdir)
		return;

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_MESH_POINT:
#ifdef CONFIG_MAC80211_MESH
		del_mesh_stats(sdata);
		del_mesh_config(sdata);
#endif
		break;
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_ADHOC:
		del_sta_files(sdata);
		break;
	case NL80211_IFTYPE_AP:
		del_ap_files(sdata);
		break;
	case NL80211_IFTYPE_WDS:
		del_wds_files(sdata);
		break;
	case NL80211_IFTYPE_MONITOR:
		del_monitor_files(sdata);
		break;
	case NL80211_IFTYPE_AP_VLAN:
		del_vlan_files(sdata);
		break;
	default:
		break;
	}
}

static int notif_registered;

void ieee80211_debugfs_add_netdev(struct ieee80211_sub_if_data *sdata)
{
	char buf[10+IFNAMSIZ];

	if (!notif_registered)
		return;

	sprintf(buf, "netdev:%s", sdata->dev->name);
	sdata->debugfsdir = debugfs_create_dir(buf,
		sdata->local->hw.wiphy->debugfsdir);
	add_files(sdata);
}

void ieee80211_debugfs_remove_netdev(struct ieee80211_sub_if_data *sdata)
{
	del_files(sdata);
	debugfs_remove(sdata->debugfsdir);
	sdata->debugfsdir = NULL;
}

static int netdev_notify(struct notifier_block *nb,
			 unsigned long state,
			 void *ndev)
{
	struct net_device *dev = ndev;
	struct dentry *dir;
	struct ieee80211_sub_if_data *sdata;
	char buf[10+IFNAMSIZ];

	if (state != NETDEV_CHANGENAME)
		return 0;

	if (!dev->ieee80211_ptr || !dev->ieee80211_ptr->wiphy)
		return 0;

	if (dev->ieee80211_ptr->wiphy->privid != mac80211_wiphy_privid)
		return 0;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	sprintf(buf, "netdev:%s", dev->name);
	dir = sdata->debugfsdir;
	if (!debugfs_rename(dir->d_parent, dir, dir->d_parent, buf))
		printk(KERN_ERR "mac80211: debugfs: failed to rename debugfs "
		       "dir to %s\n", buf);

	return 0;
}

static struct notifier_block mac80211_debugfs_netdev_notifier = {
	.notifier_call = netdev_notify,
};

void ieee80211_debugfs_netdev_init(void)
{
	int err;

	err = register_netdevice_notifier(&mac80211_debugfs_netdev_notifier);
	if (err) {
		printk(KERN_ERR
		       "mac80211: failed to install netdev notifier,"
		       " disabling per-netdev debugfs!\n");
	} else
		notif_registered = 1;
}

void ieee80211_debugfs_netdev_exit(void)
{
	unregister_netdevice_notifier(&mac80211_debugfs_netdev_notifier);
	notif_registered = 0;
}
