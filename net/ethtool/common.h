/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _ETHTOOL_COMMON_H
#define _ETHTOOL_COMMON_H

#include <linux/netdevice.h>
#include <linux/ethtool.h>

#define ETHTOOL_DEV_FEATURE_WORDS	DIV_ROUND_UP(NETDEV_FEATURE_COUNT, 32)

/* compose link mode index from speed, type and duplex */
#define ETHTOOL_LINK_MODE(speed, type, duplex) \
	ETHTOOL_LINK_MODE_ ## speed ## base ## type ## _ ## duplex ## _BIT

#define __SOF_TIMESTAMPING_CNT (const_ilog2(SOF_TIMESTAMPING_LAST) + 1)

struct link_mode_info {
	int				speed;
	u8				lanes;
	u8				duplex;
};

extern const char
netdev_features_strings[NETDEV_FEATURE_COUNT][ETH_GSTRING_LEN];
extern const char
rss_hash_func_strings[ETH_RSS_HASH_FUNCS_COUNT][ETH_GSTRING_LEN];
extern const char
tunable_strings[__ETHTOOL_TUNABLE_COUNT][ETH_GSTRING_LEN];
extern const char
phy_tunable_strings[__ETHTOOL_PHY_TUNABLE_COUNT][ETH_GSTRING_LEN];
extern const char link_mode_names[][ETH_GSTRING_LEN];
extern const struct link_mode_info link_mode_params[];
extern const char netif_msg_class_names[][ETH_GSTRING_LEN];
extern const char wol_mode_names[][ETH_GSTRING_LEN];
extern const char sof_timestamping_names[][ETH_GSTRING_LEN];
extern const char ts_tx_type_names[][ETH_GSTRING_LEN];
extern const char ts_rx_filter_names[][ETH_GSTRING_LEN];
extern const char udp_tunnel_type_names[][ETH_GSTRING_LEN];

int __ethtool_get_link(struct net_device *dev);

bool convert_legacy_settings_to_link_ksettings(
	struct ethtool_link_ksettings *link_ksettings,
	const struct ethtool_cmd *legacy_settings);
int ethtool_get_max_rxfh_channel(struct net_device *dev, u32 *max);
int ethtool_get_max_rxnfc_channel(struct net_device *dev, u64 *max);
int __ethtool_get_ts_info(struct net_device *dev, struct ethtool_ts_info *info);

extern const struct ethtool_phy_ops *ethtool_phy_ops;
extern const struct ethtool_pse_ops *ethtool_pse_ops;

int ethtool_get_module_info_call(struct net_device *dev,
				 struct ethtool_modinfo *modinfo);
int ethtool_get_module_eeprom_call(struct net_device *dev,
				   struct ethtool_eeprom *ee, u8 *data);

#endif /* _ETHTOOL_COMMON_H */
