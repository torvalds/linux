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
