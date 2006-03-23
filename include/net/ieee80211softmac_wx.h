/*
 * This file contains the prototypes for the wireless extension
 * handlers that the softmac API provides. Include this file to
 * use the wx handlers, you can assign these directly.
 *
 * Copyright (c) 2005 Johannes Berg <johannes@sipsolutions.net>
 *                    Joseph Jezak <josejx@gentoo.org>
 *                    Larry Finger <Larry.Finger@lwfinger.net>
 *                    Danny van Dyk <kugelfang@gentoo.org>
 *                    Michael Buesch <mbuesch@freenet.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called COPYING.
 */

#ifndef _IEEE80211SOFTMAC_WX_H
#define _IEEE80211SOFTMAC_WX_H

#include <net/ieee80211softmac.h>
#include <net/iw_handler.h>

extern int
ieee80211softmac_wx_trigger_scan(struct net_device *net_dev,
				 struct iw_request_info *info,
				 union iwreq_data *data,
				 char *extra);

extern int
ieee80211softmac_wx_get_scan_results(struct net_device *net_dev,
				     struct iw_request_info *info,
				     union iwreq_data *data,
				     char *extra);

extern int
ieee80211softmac_wx_set_essid(struct net_device *net_dev,
			      struct iw_request_info *info,
			      union iwreq_data *data,
			      char *extra);

extern int
ieee80211softmac_wx_get_essid(struct net_device *net_dev,
			      struct iw_request_info *info,
			      union iwreq_data *data,
			      char *extra);

extern int
ieee80211softmac_wx_set_rate(struct net_device *net_dev,
			     struct iw_request_info *info,
			     union iwreq_data *data,
			     char *extra);

extern int
ieee80211softmac_wx_get_rate(struct net_device *net_dev,
			     struct iw_request_info *info,
			     union iwreq_data *data,
			     char *extra);

extern int
ieee80211softmac_wx_get_wap(struct net_device *net_dev,
			      struct iw_request_info *info,
			      union iwreq_data *data,
			      char *extra);

extern int
ieee80211softmac_wx_set_wap(struct net_device *net_dev,
			      struct iw_request_info *info,
			      union iwreq_data *data,
			      char *extra);

extern int
ieee80211softmac_wx_set_genie(struct net_device *dev,
			      struct iw_request_info *info,
			      union iwreq_data *wrqu,
			      char *extra);

extern int
ieee80211softmac_wx_get_genie(struct net_device *dev,
			      struct iw_request_info *info,
			      union iwreq_data *wrqu,
			      char *extra);
#endif /* _IEEE80211SOFTMAC_WX */
