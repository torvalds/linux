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
#define __HWTSTAMP_FLAG_CNT (const_ilog2(HWTSTAMP_FLAG_LAST) + 1)

struct genl_info;
struct hwtstamp_provider_desc;

extern const char
netdev_features_strings[NETDEV_FEATURE_COUNT][ETH_GSTRING_LEN];
extern const char
rss_hash_func_strings[ETH_RSS_HASH_FUNCS_COUNT][ETH_GSTRING_LEN];
extern const char
tunable_strings[__ETHTOOL_TUNABLE_COUNT][ETH_GSTRING_LEN];
extern const char
phy_tunable_strings[__ETHTOOL_PHY_TUNABLE_COUNT][ETH_GSTRING_LEN];
extern const char link_mode_names[][ETH_GSTRING_LEN];
extern const char netif_msg_class_names[][ETH_GSTRING_LEN];
extern const char wol_mode_names[][ETH_GSTRING_LEN];
extern const char sof_timestamping_names[][ETH_GSTRING_LEN];
extern const char ts_tx_type_names[][ETH_GSTRING_LEN];
extern const char ts_rx_filter_names[][ETH_GSTRING_LEN];
extern const char ts_flags_names[][ETH_GSTRING_LEN];
extern const char udp_tunnel_type_names[][ETH_GSTRING_LEN];

int __ethtool_get_link(struct net_device *dev);

bool convert_legacy_settings_to_link_ksettings(
	struct ethtool_link_ksettings *link_ksettings,
	const struct ethtool_cmd *legacy_settings);
int ethtool_check_max_channel(struct net_device *dev,
			      struct ethtool_channels channels,
			      struct genl_info *info);
struct ethtool_rxfh_context *
ethtool_rxfh_ctx_alloc(const struct ethtool_ops *ops,
		       u32 indir_size, u32 key_size);
int ethtool_check_rss_ctx_busy(struct net_device *dev, u32 rss_context);
int ethtool_rxfh_config_is_sym(u64 rxfh);

void ethtool_ringparam_get_cfg(struct net_device *dev,
			       struct ethtool_ringparam *param,
			       struct kernel_ethtool_ringparam *kparam,
			       struct netlink_ext_ack *extack);

int ethtool_get_rx_ring_count(struct net_device *dev);

int __ethtool_get_ts_info(struct net_device *dev, struct kernel_ethtool_ts_info *info);
int ethtool_get_ts_info_by_phc(struct net_device *dev,
			       struct kernel_ethtool_ts_info *info,
			       struct hwtstamp_provider_desc *hwprov_desc);
int ethtool_net_get_ts_info_by_phc(struct net_device *dev,
				   struct kernel_ethtool_ts_info *info,
				   struct hwtstamp_provider_desc *hwprov_desc);
struct phy_device *
ethtool_phy_get_ts_info_by_phc(struct net_device *dev,
			       struct kernel_ethtool_ts_info *info,
			       struct hwtstamp_provider_desc *hwprov_desc);
bool net_support_hwtstamp_qualifier(struct net_device *dev,
				    enum hwtstamp_provider_qualifier qualifier);

extern const struct ethtool_phy_ops *ethtool_phy_ops;
extern const struct ethtool_pse_ops *ethtool_pse_ops;

int ethtool_get_module_info_call(struct net_device *dev,
				 struct ethtool_modinfo *modinfo);
int ethtool_get_module_eeprom_call(struct net_device *dev,
				   struct ethtool_eeprom *ee, u8 *data);

bool __ethtool_dev_mm_supported(struct net_device *dev);

#if IS_ENABLED(CONFIG_ETHTOOL_NETLINK)
void ethtool_rss_notify(struct net_device *dev, u32 type, u32 rss_context);
#else
static inline void
ethtool_rss_notify(struct net_device *dev, u32 type, u32 rss_context)
{
}
#endif

#endif /* _ETHTOOL_COMMON_H */
