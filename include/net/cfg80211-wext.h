/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __NET_CFG80211_WEXT_H
#define __NET_CFG80211_WEXT_H
/*
 * 802.11 device and configuration interface -- wext handlers
 *
 * Copyright 2006-2010	Johannes Berg <johannes@sipsolutions.net>
 */

#include <linux/netdevice.h>
#include <linux/wireless.h>
#include <net/iw_handler.h>

/*
 * Temporary wext handlers & helper functions
 *
 * These are used only by drivers that aren't yet fully
 * converted to cfg80211.
 */
int cfg80211_wext_giwname(struct net_device *dev,
			  struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra);
int cfg80211_wext_siwmode(struct net_device *dev, struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra);
int cfg80211_wext_giwmode(struct net_device *dev, struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra);
int cfg80211_wext_siwscan(struct net_device *dev,
			  struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra);
int cfg80211_wext_giwscan(struct net_device *dev,
			  struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra);
int cfg80211_wext_giwrange(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra);
int cfg80211_wext_siwrts(struct net_device *dev,
			 struct iw_request_info *info,
			 union iwreq_data *wrqu, char *extra);
int cfg80211_wext_giwrts(struct net_device *dev,
			 struct iw_request_info *info,
			 union iwreq_data *wrqu, char *extra);
int cfg80211_wext_siwfrag(struct net_device *dev,
			  struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra);
int cfg80211_wext_giwfrag(struct net_device *dev,
			  struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra);
int cfg80211_wext_giwretry(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra);

#endif /* __NET_CFG80211_WEXT_H */
