#ifndef __NET_CFG80211_WEXT_H
#define __NET_CFG80211_WEXT_H
/*
 * 802.11 device and configuration interface -- wext handlers
 *
 * Copyright 2006-2010	Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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
			  char *name, char *extra);
int cfg80211_wext_siwmode(struct net_device *dev, struct iw_request_info *info,
			  u32 *mode, char *extra);
int cfg80211_wext_giwmode(struct net_device *dev, struct iw_request_info *info,
			  u32 *mode, char *extra);
int cfg80211_wext_siwscan(struct net_device *dev,
			  struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra);
int cfg80211_wext_giwscan(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_point *data, char *extra);
int cfg80211_wext_siwmlme(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_point *data, char *extra);
int cfg80211_wext_giwrange(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_point *data, char *extra);
int cfg80211_wext_siwgenie(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_point *data, char *extra);
int cfg80211_wext_siwauth(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_param *data, char *extra);
int cfg80211_wext_giwauth(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_param *data, char *extra);

int cfg80211_wext_siwfreq(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_freq *freq, char *extra);
int cfg80211_wext_giwfreq(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_freq *freq, char *extra);
int cfg80211_wext_siwessid(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_point *data, char *ssid);
int cfg80211_wext_giwessid(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_point *data, char *ssid);
int cfg80211_wext_siwrate(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_param *rate, char *extra);
int cfg80211_wext_giwrate(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_param *rate, char *extra);

int cfg80211_wext_siwrts(struct net_device *dev,
			 struct iw_request_info *info,
			 struct iw_param *rts, char *extra);
int cfg80211_wext_giwrts(struct net_device *dev,
			 struct iw_request_info *info,
			 struct iw_param *rts, char *extra);
int cfg80211_wext_siwfrag(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_param *frag, char *extra);
int cfg80211_wext_giwfrag(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_param *frag, char *extra);
int cfg80211_wext_siwretry(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_param *retry, char *extra);
int cfg80211_wext_giwretry(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_param *retry, char *extra);
int cfg80211_wext_siwencodeext(struct net_device *dev,
			       struct iw_request_info *info,
			       struct iw_point *erq, char *extra);
int cfg80211_wext_siwencode(struct net_device *dev,
			    struct iw_request_info *info,
			    struct iw_point *erq, char *keybuf);
int cfg80211_wext_giwencode(struct net_device *dev,
			    struct iw_request_info *info,
			    struct iw_point *erq, char *keybuf);
int cfg80211_wext_siwtxpower(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *data, char *keybuf);
int cfg80211_wext_giwtxpower(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *data, char *keybuf);
struct iw_statistics *cfg80211_wireless_stats(struct net_device *dev);

int cfg80211_wext_siwpower(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_param *wrq, char *extra);
int cfg80211_wext_giwpower(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_param *wrq, char *extra);

int cfg80211_wext_siwap(struct net_device *dev,
			struct iw_request_info *info,
			struct sockaddr *ap_addr, char *extra);
int cfg80211_wext_giwap(struct net_device *dev,
			struct iw_request_info *info,
			struct sockaddr *ap_addr, char *extra);

int cfg80211_wext_siwpmksa(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_point *data, char *extra);

#endif /* __NET_CFG80211_WEXT_H */
