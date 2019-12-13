/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _ETHTOOL_COMMON_H
#define _ETHTOOL_COMMON_H

#include <linux/ethtool.h>

/* compose link mode index from speed, type and duplex */
#define ETHTOOL_LINK_MODE(speed, type, duplex) \
	ETHTOOL_LINK_MODE_ ## speed ## base ## type ## _ ## duplex ## _BIT

extern const char
netdev_features_strings[NETDEV_FEATURE_COUNT][ETH_GSTRING_LEN];
extern const char
rss_hash_func_strings[ETH_RSS_HASH_FUNCS_COUNT][ETH_GSTRING_LEN];
extern const char
tunable_strings[__ETHTOOL_TUNABLE_COUNT][ETH_GSTRING_LEN];
extern const char
phy_tunable_strings[__ETHTOOL_PHY_TUNABLE_COUNT][ETH_GSTRING_LEN];
extern const char link_mode_names[][ETH_GSTRING_LEN];

#endif /* _ETHTOOL_COMMON_H */
