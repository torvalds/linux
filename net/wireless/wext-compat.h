#ifndef __WEXT_COMPAT
#define __WEXT_COMPAT

int cfg80211_ibss_wext_siwfreq(struct net_device *dev,
			       struct iw_request_info *info,
			       struct iw_freq *freq, char *extra);
int cfg80211_ibss_wext_giwfreq(struct net_device *dev,
			       struct iw_request_info *info,
			       struct iw_freq *freq, char *extra);
int cfg80211_ibss_wext_siwap(struct net_device *dev,
			     struct iw_request_info *info,
			     struct sockaddr *ap_addr, char *extra);
int cfg80211_ibss_wext_giwap(struct net_device *dev,
			     struct iw_request_info *info,
			     struct sockaddr *ap_addr, char *extra);

int cfg80211_mgd_wext_siwfreq(struct net_device *dev,
			      struct iw_request_info *info,
			      struct iw_freq *freq, char *extra);
int cfg80211_mgd_wext_giwfreq(struct net_device *dev,
			      struct iw_request_info *info,
			      struct iw_freq *freq, char *extra);
int cfg80211_mgd_wext_siwap(struct net_device *dev,
			    struct iw_request_info *info,
			    struct sockaddr *ap_addr, char *extra);
int cfg80211_mgd_wext_giwap(struct net_device *dev,
			    struct iw_request_info *info,
			    struct sockaddr *ap_addr, char *extra);

struct ieee80211_channel *cfg80211_wext_freq(struct wiphy *wiphy,
					     struct iw_freq *freq);

#endif /* __WEXT_COMPAT */
