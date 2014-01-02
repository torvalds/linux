#ifndef __MAC80211_DEBUG_H
#define __MAC80211_DEBUG_H
#include <net/cfg80211.h>

#ifdef CONFIG_MAC80211_IBSS_DEBUG
#define MAC80211_IBSS_DEBUG 1
#else
#define MAC80211_IBSS_DEBUG 0
#endif

#ifdef CONFIG_MAC80211_PS_DEBUG
#define MAC80211_PS_DEBUG 1
#else
#define MAC80211_PS_DEBUG 0
#endif

#ifdef CONFIG_MAC80211_HT_DEBUG
#define MAC80211_HT_DEBUG 1
#else
#define MAC80211_HT_DEBUG 0
#endif

#ifdef CONFIG_MAC80211_MPL_DEBUG
#define MAC80211_MPL_DEBUG 1
#else
#define MAC80211_MPL_DEBUG 0
#endif

#ifdef CONFIG_MAC80211_MPATH_DEBUG
#define MAC80211_MPATH_DEBUG 1
#else
#define MAC80211_MPATH_DEBUG 0
#endif

#ifdef CONFIG_MAC80211_MHWMP_DEBUG
#define MAC80211_MHWMP_DEBUG 1
#else
#define MAC80211_MHWMP_DEBUG 0
#endif

#ifdef CONFIG_MAC80211_MESH_SYNC_DEBUG
#define MAC80211_MESH_SYNC_DEBUG 1
#else
#define MAC80211_MESH_SYNC_DEBUG 0
#endif

#ifdef CONFIG_MAC80211_MESH_CSA_DEBUG
#define MAC80211_MESH_CSA_DEBUG 1
#else
#define MAC80211_MESH_CSA_DEBUG 0
#endif

#ifdef CONFIG_MAC80211_MESH_PS_DEBUG
#define MAC80211_MESH_PS_DEBUG 1
#else
#define MAC80211_MESH_PS_DEBUG 0
#endif

#ifdef CONFIG_MAC80211_TDLS_DEBUG
#define MAC80211_TDLS_DEBUG 1
#else
#define MAC80211_TDLS_DEBUG 0
#endif

#ifdef CONFIG_MAC80211_STA_DEBUG
#define MAC80211_STA_DEBUG 1
#else
#define MAC80211_STA_DEBUG 0
#endif

#ifdef CONFIG_MAC80211_MLME_DEBUG
#define MAC80211_MLME_DEBUG 1
#else
#define MAC80211_MLME_DEBUG 0
#endif

#ifdef CONFIG_MAC80211_MESSAGE_TRACING
void __sdata_info(const char *fmt, ...) __printf(1, 2);
void __sdata_dbg(bool print, const char *fmt, ...) __printf(2, 3);
void __sdata_err(const char *fmt, ...) __printf(1, 2);
void __wiphy_dbg(struct wiphy *wiphy, bool print, const char *fmt, ...)
	__printf(3, 4);

#define _sdata_info(sdata, fmt, ...)					\
	__sdata_info("%s: " fmt, (sdata)->name, ##__VA_ARGS__)
#define _sdata_dbg(print, sdata, fmt, ...)				\
	__sdata_dbg(print, "%s: " fmt, (sdata)->name, ##__VA_ARGS__)
#define _sdata_err(sdata, fmt, ...)					\
	__sdata_err("%s: " fmt, (sdata)->name, ##__VA_ARGS__)
#define _wiphy_dbg(print, wiphy, fmt, ...)				\
	__wiphy_dbg(wiphy, print, fmt, ##__VA_ARGS__)
#else
#define _sdata_info(sdata, fmt, ...)					\
do {									\
	pr_info("%s: " fmt,						\
		(sdata)->name, ##__VA_ARGS__);				\
} while (0)

#define _sdata_dbg(print, sdata, fmt, ...)				\
do {									\
	if (print)							\
		pr_debug("%s: " fmt,					\
			 (sdata)->name, ##__VA_ARGS__);			\
} while (0)

#define _sdata_err(sdata, fmt, ...)					\
do {									\
	pr_err("%s: " fmt,						\
	       (sdata)->name, ##__VA_ARGS__);				\
} while (0)

#define _wiphy_dbg(print, wiphy, fmt, ...)				\
do {									\
	if (print)							\
		wiphy_dbg((wiphy), fmt, ##__VA_ARGS__);			\
} while (0)
#endif

#define sdata_info(sdata, fmt, ...)					\
	_sdata_info(sdata, fmt, ##__VA_ARGS__)
#define sdata_err(sdata, fmt, ...)					\
	_sdata_err(sdata, fmt, ##__VA_ARGS__)
#define sdata_dbg(sdata, fmt, ...)					\
	_sdata_dbg(1, sdata, fmt, ##__VA_ARGS__)

#define ht_dbg(sdata, fmt, ...)						\
	_sdata_dbg(MAC80211_HT_DEBUG,					\
		   sdata, fmt, ##__VA_ARGS__)

#define ht_dbg_ratelimited(sdata, fmt, ...)				\
	_sdata_dbg(MAC80211_HT_DEBUG && net_ratelimit(),		\
		   sdata, fmt, ##__VA_ARGS__)

#define ibss_dbg(sdata, fmt, ...)					\
	_sdata_dbg(MAC80211_IBSS_DEBUG,					\
		   sdata, fmt, ##__VA_ARGS__)

#define ps_dbg(sdata, fmt, ...)						\
	_sdata_dbg(MAC80211_PS_DEBUG,					\
		   sdata, fmt, ##__VA_ARGS__)

#define ps_dbg_hw(hw, fmt, ...)						\
	_wiphy_dbg(MAC80211_PS_DEBUG,					\
		   (hw)->wiphy, fmt, ##__VA_ARGS__)

#define ps_dbg_ratelimited(sdata, fmt, ...)				\
	_sdata_dbg(MAC80211_PS_DEBUG && net_ratelimit(),		\
		   sdata, fmt, ##__VA_ARGS__)

#define mpl_dbg(sdata, fmt, ...)					\
	_sdata_dbg(MAC80211_MPL_DEBUG,					\
		   sdata, fmt, ##__VA_ARGS__)

#define mpath_dbg(sdata, fmt, ...)					\
	_sdata_dbg(MAC80211_MPATH_DEBUG,				\
		   sdata, fmt, ##__VA_ARGS__)

#define mhwmp_dbg(sdata, fmt, ...)					\
	_sdata_dbg(MAC80211_MHWMP_DEBUG,				\
		   sdata, fmt, ##__VA_ARGS__)

#define msync_dbg(sdata, fmt, ...)					\
	_sdata_dbg(MAC80211_MESH_SYNC_DEBUG,				\
		   sdata, fmt, ##__VA_ARGS__)

#define mcsa_dbg(sdata, fmt, ...)					\
	_sdata_dbg(MAC80211_MESH_CSA_DEBUG,				\
		   sdata, fmt, ##__VA_ARGS__)

#define mps_dbg(sdata, fmt, ...)					\
	_sdata_dbg(MAC80211_MESH_PS_DEBUG,				\
		   sdata, fmt, ##__VA_ARGS__)

#define tdls_dbg(sdata, fmt, ...)					\
	_sdata_dbg(MAC80211_TDLS_DEBUG,					\
		   sdata, fmt, ##__VA_ARGS__)

#define sta_dbg(sdata, fmt, ...)					\
	_sdata_dbg(MAC80211_STA_DEBUG,					\
		   sdata, fmt, ##__VA_ARGS__)

#define mlme_dbg(sdata, fmt, ...)					\
	_sdata_dbg(MAC80211_MLME_DEBUG,					\
		   sdata, fmt, ##__VA_ARGS__)

#define mlme_dbg_ratelimited(sdata, fmt, ...)				\
	_sdata_dbg(MAC80211_MLME_DEBUG && net_ratelimit(),		\
		   sdata, fmt, ##__VA_ARGS__)

#endif /* __MAC80211_DEBUG_H */
