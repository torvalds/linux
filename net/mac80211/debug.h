/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MAC80211_DE_H
#define __MAC80211_DE_H
#include <net/cfg80211.h>

#ifdef CONFIG_MAC80211_OCB_DE
#define MAC80211_OCB_DE 1
#else
#define MAC80211_OCB_DE 0
#endif

#ifdef CONFIG_MAC80211_IBSS_DE
#define MAC80211_IBSS_DE 1
#else
#define MAC80211_IBSS_DE 0
#endif

#ifdef CONFIG_MAC80211_PS_DE
#define MAC80211_PS_DE 1
#else
#define MAC80211_PS_DE 0
#endif

#ifdef CONFIG_MAC80211_HT_DE
#define MAC80211_HT_DE 1
#else
#define MAC80211_HT_DE 0
#endif

#ifdef CONFIG_MAC80211_MPL_DE
#define MAC80211_MPL_DE 1
#else
#define MAC80211_MPL_DE 0
#endif

#ifdef CONFIG_MAC80211_MPATH_DE
#define MAC80211_MPATH_DE 1
#else
#define MAC80211_MPATH_DE 0
#endif

#ifdef CONFIG_MAC80211_MHWMP_DE
#define MAC80211_MHWMP_DE 1
#else
#define MAC80211_MHWMP_DE 0
#endif

#ifdef CONFIG_MAC80211_MESH_SYNC_DE
#define MAC80211_MESH_SYNC_DE 1
#else
#define MAC80211_MESH_SYNC_DE 0
#endif

#ifdef CONFIG_MAC80211_MESH_CSA_DE
#define MAC80211_MESH_CSA_DE 1
#else
#define MAC80211_MESH_CSA_DE 0
#endif

#ifdef CONFIG_MAC80211_MESH_PS_DE
#define MAC80211_MESH_PS_DE 1
#else
#define MAC80211_MESH_PS_DE 0
#endif

#ifdef CONFIG_MAC80211_TDLS_DE
#define MAC80211_TDLS_DE 1
#else
#define MAC80211_TDLS_DE 0
#endif

#ifdef CONFIG_MAC80211_STA_DE
#define MAC80211_STA_DE 1
#else
#define MAC80211_STA_DE 0
#endif

#ifdef CONFIG_MAC80211_MLME_DE
#define MAC80211_MLME_DE 1
#else
#define MAC80211_MLME_DE 0
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
		pr_de("%s: " fmt,					\
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
	_sdata_dbg(MAC80211_HT_DE,					\
		   sdata, fmt, ##__VA_ARGS__)

#define ht_dbg_ratelimited(sdata, fmt, ...)				\
	_sdata_dbg(MAC80211_HT_DE && net_ratelimit(),		\
		   sdata, fmt, ##__VA_ARGS__)

#define ocb_dbg(sdata, fmt, ...)					\
	_sdata_dbg(MAC80211_OCB_DE,					\
		   sdata, fmt, ##__VA_ARGS__)

#define ibss_dbg(sdata, fmt, ...)					\
	_sdata_dbg(MAC80211_IBSS_DE,					\
		   sdata, fmt, ##__VA_ARGS__)

#define ps_dbg(sdata, fmt, ...)						\
	_sdata_dbg(MAC80211_PS_DE,					\
		   sdata, fmt, ##__VA_ARGS__)

#define ps_dbg_hw(hw, fmt, ...)						\
	_wiphy_dbg(MAC80211_PS_DE,					\
		   (hw)->wiphy, fmt, ##__VA_ARGS__)

#define ps_dbg_ratelimited(sdata, fmt, ...)				\
	_sdata_dbg(MAC80211_PS_DE && net_ratelimit(),		\
		   sdata, fmt, ##__VA_ARGS__)

#define mpl_dbg(sdata, fmt, ...)					\
	_sdata_dbg(MAC80211_MPL_DE,					\
		   sdata, fmt, ##__VA_ARGS__)

#define mpath_dbg(sdata, fmt, ...)					\
	_sdata_dbg(MAC80211_MPATH_DE,				\
		   sdata, fmt, ##__VA_ARGS__)

#define mhwmp_dbg(sdata, fmt, ...)					\
	_sdata_dbg(MAC80211_MHWMP_DE,				\
		   sdata, fmt, ##__VA_ARGS__)

#define msync_dbg(sdata, fmt, ...)					\
	_sdata_dbg(MAC80211_MESH_SYNC_DE,				\
		   sdata, fmt, ##__VA_ARGS__)

#define mcsa_dbg(sdata, fmt, ...)					\
	_sdata_dbg(MAC80211_MESH_CSA_DE,				\
		   sdata, fmt, ##__VA_ARGS__)

#define mps_dbg(sdata, fmt, ...)					\
	_sdata_dbg(MAC80211_MESH_PS_DE,				\
		   sdata, fmt, ##__VA_ARGS__)

#define tdls_dbg(sdata, fmt, ...)					\
	_sdata_dbg(MAC80211_TDLS_DE,					\
		   sdata, fmt, ##__VA_ARGS__)

#define sta_dbg(sdata, fmt, ...)					\
	_sdata_dbg(MAC80211_STA_DE,					\
		   sdata, fmt, ##__VA_ARGS__)

#define mlme_dbg(sdata, fmt, ...)					\
	_sdata_dbg(MAC80211_MLME_DE,					\
		   sdata, fmt, ##__VA_ARGS__)

#define mlme_dbg_ratelimited(sdata, fmt, ...)				\
	_sdata_dbg(MAC80211_MLME_DE && net_ratelimit(),		\
		   sdata, fmt, ##__VA_ARGS__)

#endif /* __MAC80211_DE_H */
