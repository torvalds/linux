// SPDX-License-Identifier: GPL-2.0-only

#include <linux/net_tstamp.h>
#include <linux/phy.h>

#include "common.h"

const char netdev_features_strings[NETDEV_FEATURE_COUNT][ETH_GSTRING_LEN] = {
	[NETIF_F_SG_BIT] =               "tx-scatter-gather",
	[NETIF_F_IP_CSUM_BIT] =          "tx-checksum-ipv4",
	[NETIF_F_HW_CSUM_BIT] =          "tx-checksum-ip-generic",
	[NETIF_F_IPV6_CSUM_BIT] =        "tx-checksum-ipv6",
	[NETIF_F_HIGHDMA_BIT] =          "highdma",
	[NETIF_F_FRAGLIST_BIT] =         "tx-scatter-gather-fraglist",
	[NETIF_F_HW_VLAN_CTAG_TX_BIT] =  "tx-vlan-hw-insert",

	[NETIF_F_HW_VLAN_CTAG_RX_BIT] =  "rx-vlan-hw-parse",
	[NETIF_F_HW_VLAN_CTAG_FILTER_BIT] = "rx-vlan-filter",
	[NETIF_F_HW_VLAN_STAG_TX_BIT] =  "tx-vlan-stag-hw-insert",
	[NETIF_F_HW_VLAN_STAG_RX_BIT] =  "rx-vlan-stag-hw-parse",
	[NETIF_F_HW_VLAN_STAG_FILTER_BIT] = "rx-vlan-stag-filter",
	[NETIF_F_VLAN_CHALLENGED_BIT] =  "vlan-challenged",
	[NETIF_F_GSO_BIT] =              "tx-generic-segmentation",
	[NETIF_F_LLTX_BIT] =             "tx-lockless",
	[NETIF_F_NETNS_LOCAL_BIT] =      "netns-local",
	[NETIF_F_GRO_BIT] =              "rx-gro",
	[NETIF_F_GRO_HW_BIT] =           "rx-gro-hw",
	[NETIF_F_LRO_BIT] =              "rx-lro",

	[NETIF_F_TSO_BIT] =              "tx-tcp-segmentation",
	[NETIF_F_GSO_ROBUST_BIT] =       "tx-gso-robust",
	[NETIF_F_TSO_ECN_BIT] =          "tx-tcp-ecn-segmentation",
	[NETIF_F_TSO_MANGLEID_BIT] =	 "tx-tcp-mangleid-segmentation",
	[NETIF_F_TSO6_BIT] =             "tx-tcp6-segmentation",
	[NETIF_F_FSO_BIT] =              "tx-fcoe-segmentation",
	[NETIF_F_GSO_GRE_BIT] =		 "tx-gre-segmentation",
	[NETIF_F_GSO_GRE_CSUM_BIT] =	 "tx-gre-csum-segmentation",
	[NETIF_F_GSO_IPXIP4_BIT] =	 "tx-ipxip4-segmentation",
	[NETIF_F_GSO_IPXIP6_BIT] =	 "tx-ipxip6-segmentation",
	[NETIF_F_GSO_UDP_TUNNEL_BIT] =	 "tx-udp_tnl-segmentation",
	[NETIF_F_GSO_UDP_TUNNEL_CSUM_BIT] = "tx-udp_tnl-csum-segmentation",
	[NETIF_F_GSO_PARTIAL_BIT] =	 "tx-gso-partial",
	[NETIF_F_GSO_SCTP_BIT] =	 "tx-sctp-segmentation",
	[NETIF_F_GSO_ESP_BIT] =		 "tx-esp-segmentation",
	[NETIF_F_GSO_UDP_L4_BIT] =	 "tx-udp-segmentation",

	[NETIF_F_FCOE_CRC_BIT] =         "tx-checksum-fcoe-crc",
	[NETIF_F_SCTP_CRC_BIT] =        "tx-checksum-sctp",
	[NETIF_F_FCOE_MTU_BIT] =         "fcoe-mtu",
	[NETIF_F_NTUPLE_BIT] =           "rx-ntuple-filter",
	[NETIF_F_RXHASH_BIT] =           "rx-hashing",
	[NETIF_F_RXCSUM_BIT] =           "rx-checksum",
	[NETIF_F_NOCACHE_COPY_BIT] =     "tx-nocache-copy",
	[NETIF_F_LOOPBACK_BIT] =         "loopback",
	[NETIF_F_RXFCS_BIT] =            "rx-fcs",
	[NETIF_F_RXALL_BIT] =            "rx-all",
	[NETIF_F_HW_L2FW_DOFFLOAD_BIT] = "l2-fwd-offload",
	[NETIF_F_HW_TC_BIT] =		 "hw-tc-offload",
	[NETIF_F_HW_ESP_BIT] =		 "esp-hw-offload",
	[NETIF_F_HW_ESP_TX_CSUM_BIT] =	 "esp-tx-csum-hw-offload",
	[NETIF_F_RX_UDP_TUNNEL_PORT_BIT] =	 "rx-udp_tunnel-port-offload",
	[NETIF_F_HW_TLS_RECORD_BIT] =	"tls-hw-record",
	[NETIF_F_HW_TLS_TX_BIT] =	 "tls-hw-tx-offload",
	[NETIF_F_HW_TLS_RX_BIT] =	 "tls-hw-rx-offload",
	[NETIF_F_GRO_FRAGLIST_BIT] =	 "rx-gro-list",
	[NETIF_F_HW_MACSEC_BIT] =	 "macsec-hw-offload",
};

const char
rss_hash_func_strings[ETH_RSS_HASH_FUNCS_COUNT][ETH_GSTRING_LEN] = {
	[ETH_RSS_HASH_TOP_BIT] =	"toeplitz",
	[ETH_RSS_HASH_XOR_BIT] =	"xor",
	[ETH_RSS_HASH_CRC32_BIT] =	"crc32",
};

const char
tunable_strings[__ETHTOOL_TUNABLE_COUNT][ETH_GSTRING_LEN] = {
	[ETHTOOL_ID_UNSPEC]     = "Unspec",
	[ETHTOOL_RX_COPYBREAK]	= "rx-copybreak",
	[ETHTOOL_TX_COPYBREAK]	= "tx-copybreak",
	[ETHTOOL_PFC_PREVENTION_TOUT] = "pfc-prevention-tout",
};

const char
phy_tunable_strings[__ETHTOOL_PHY_TUNABLE_COUNT][ETH_GSTRING_LEN] = {
	[ETHTOOL_ID_UNSPEC]     = "Unspec",
	[ETHTOOL_PHY_DOWNSHIFT]	= "phy-downshift",
	[ETHTOOL_PHY_FAST_LINK_DOWN] = "phy-fast-link-down",
	[ETHTOOL_PHY_EDPD]	= "phy-energy-detect-power-down",
};

#define __LINK_MODE_NAME(speed, type, duplex) \
	#speed "base" #type "/" #duplex
#define __DEFINE_LINK_MODE_NAME(speed, type, duplex) \
	[ETHTOOL_LINK_MODE(speed, type, duplex)] = \
	__LINK_MODE_NAME(speed, type, duplex)
#define __DEFINE_SPECIAL_MODE_NAME(_mode, _name) \
	[ETHTOOL_LINK_MODE_ ## _mode ## _BIT] = _name

const char link_mode_names[][ETH_GSTRING_LEN] = {
	__DEFINE_LINK_MODE_NAME(10, T, Half),
	__DEFINE_LINK_MODE_NAME(10, T, Full),
	__DEFINE_LINK_MODE_NAME(100, T, Half),
	__DEFINE_LINK_MODE_NAME(100, T, Full),
	__DEFINE_LINK_MODE_NAME(1000, T, Half),
	__DEFINE_LINK_MODE_NAME(1000, T, Full),
	__DEFINE_SPECIAL_MODE_NAME(Autoneg, "Autoneg"),
	__DEFINE_SPECIAL_MODE_NAME(TP, "TP"),
	__DEFINE_SPECIAL_MODE_NAME(AUI, "AUI"),
	__DEFINE_SPECIAL_MODE_NAME(MII, "MII"),
	__DEFINE_SPECIAL_MODE_NAME(FIBRE, "FIBRE"),
	__DEFINE_SPECIAL_MODE_NAME(BNC, "BNC"),
	__DEFINE_LINK_MODE_NAME(10000, T, Full),
	__DEFINE_SPECIAL_MODE_NAME(Pause, "Pause"),
	__DEFINE_SPECIAL_MODE_NAME(Asym_Pause, "Asym_Pause"),
	__DEFINE_LINK_MODE_NAME(2500, X, Full),
	__DEFINE_SPECIAL_MODE_NAME(Backplane, "Backplane"),
	__DEFINE_LINK_MODE_NAME(1000, KX, Full),
	__DEFINE_LINK_MODE_NAME(10000, KX4, Full),
	__DEFINE_LINK_MODE_NAME(10000, KR, Full),
	__DEFINE_SPECIAL_MODE_NAME(10000baseR_FEC, "10000baseR_FEC"),
	__DEFINE_LINK_MODE_NAME(20000, MLD2, Full),
	__DEFINE_LINK_MODE_NAME(20000, KR2, Full),
	__DEFINE_LINK_MODE_NAME(40000, KR4, Full),
	__DEFINE_LINK_MODE_NAME(40000, CR4, Full),
	__DEFINE_LINK_MODE_NAME(40000, SR4, Full),
	__DEFINE_LINK_MODE_NAME(40000, LR4, Full),
	__DEFINE_LINK_MODE_NAME(56000, KR4, Full),
	__DEFINE_LINK_MODE_NAME(56000, CR4, Full),
	__DEFINE_LINK_MODE_NAME(56000, SR4, Full),
	__DEFINE_LINK_MODE_NAME(56000, LR4, Full),
	__DEFINE_LINK_MODE_NAME(25000, CR, Full),
	__DEFINE_LINK_MODE_NAME(25000, KR, Full),
	__DEFINE_LINK_MODE_NAME(25000, SR, Full),
	__DEFINE_LINK_MODE_NAME(50000, CR2, Full),
	__DEFINE_LINK_MODE_NAME(50000, KR2, Full),
	__DEFINE_LINK_MODE_NAME(100000, KR4, Full),
	__DEFINE_LINK_MODE_NAME(100000, SR4, Full),
	__DEFINE_LINK_MODE_NAME(100000, CR4, Full),
	__DEFINE_LINK_MODE_NAME(100000, LR4_ER4, Full),
	__DEFINE_LINK_MODE_NAME(50000, SR2, Full),
	__DEFINE_LINK_MODE_NAME(1000, X, Full),
	__DEFINE_LINK_MODE_NAME(10000, CR, Full),
	__DEFINE_LINK_MODE_NAME(10000, SR, Full),
	__DEFINE_LINK_MODE_NAME(10000, LR, Full),
	__DEFINE_LINK_MODE_NAME(10000, LRM, Full),
	__DEFINE_LINK_MODE_NAME(10000, ER, Full),
	__DEFINE_LINK_MODE_NAME(2500, T, Full),
	__DEFINE_LINK_MODE_NAME(5000, T, Full),
	__DEFINE_SPECIAL_MODE_NAME(FEC_NONE, "None"),
	__DEFINE_SPECIAL_MODE_NAME(FEC_RS, "RS"),
	__DEFINE_SPECIAL_MODE_NAME(FEC_BASER, "BASER"),
	__DEFINE_LINK_MODE_NAME(50000, KR, Full),
	__DEFINE_LINK_MODE_NAME(50000, SR, Full),
	__DEFINE_LINK_MODE_NAME(50000, CR, Full),
	__DEFINE_LINK_MODE_NAME(50000, LR_ER_FR, Full),
	__DEFINE_LINK_MODE_NAME(50000, DR, Full),
	__DEFINE_LINK_MODE_NAME(100000, KR2, Full),
	__DEFINE_LINK_MODE_NAME(100000, SR2, Full),
	__DEFINE_LINK_MODE_NAME(100000, CR2, Full),
	__DEFINE_LINK_MODE_NAME(100000, LR2_ER2_FR2, Full),
	__DEFINE_LINK_MODE_NAME(100000, DR2, Full),
	__DEFINE_LINK_MODE_NAME(200000, KR4, Full),
	__DEFINE_LINK_MODE_NAME(200000, SR4, Full),
	__DEFINE_LINK_MODE_NAME(200000, LR4_ER4_FR4, Full),
	__DEFINE_LINK_MODE_NAME(200000, DR4, Full),
	__DEFINE_LINK_MODE_NAME(200000, CR4, Full),
	__DEFINE_LINK_MODE_NAME(100, T1, Full),
	__DEFINE_LINK_MODE_NAME(1000, T1, Full),
	__DEFINE_LINK_MODE_NAME(400000, KR8, Full),
	__DEFINE_LINK_MODE_NAME(400000, SR8, Full),
	__DEFINE_LINK_MODE_NAME(400000, LR8_ER8_FR8, Full),
	__DEFINE_LINK_MODE_NAME(400000, DR8, Full),
	__DEFINE_LINK_MODE_NAME(400000, CR8, Full),
	__DEFINE_SPECIAL_MODE_NAME(FEC_LLRS, "LLRS"),
};
static_assert(ARRAY_SIZE(link_mode_names) == __ETHTOOL_LINK_MODE_MASK_NBITS);

const char netif_msg_class_names[][ETH_GSTRING_LEN] = {
	[NETIF_MSG_DRV_BIT]		= "drv",
	[NETIF_MSG_PROBE_BIT]		= "probe",
	[NETIF_MSG_LINK_BIT]		= "link",
	[NETIF_MSG_TIMER_BIT]		= "timer",
	[NETIF_MSG_IFDOWN_BIT]		= "ifdown",
	[NETIF_MSG_IFUP_BIT]		= "ifup",
	[NETIF_MSG_RX_ERR_BIT]		= "rx_err",
	[NETIF_MSG_TX_ERR_BIT]		= "tx_err",
	[NETIF_MSG_TX_QUEUED_BIT]	= "tx_queued",
	[NETIF_MSG_INTR_BIT]		= "intr",
	[NETIF_MSG_TX_DONE_BIT]		= "tx_done",
	[NETIF_MSG_RX_STATUS_BIT]	= "rx_status",
	[NETIF_MSG_PKTDATA_BIT]		= "pktdata",
	[NETIF_MSG_HW_BIT]		= "hw",
	[NETIF_MSG_WOL_BIT]		= "wol",
};
static_assert(ARRAY_SIZE(netif_msg_class_names) == NETIF_MSG_CLASS_COUNT);

const char wol_mode_names[][ETH_GSTRING_LEN] = {
	[const_ilog2(WAKE_PHY)]		= "phy",
	[const_ilog2(WAKE_UCAST)]	= "ucast",
	[const_ilog2(WAKE_MCAST)]	= "mcast",
	[const_ilog2(WAKE_BCAST)]	= "bcast",
	[const_ilog2(WAKE_ARP)]		= "arp",
	[const_ilog2(WAKE_MAGIC)]	= "magic",
	[const_ilog2(WAKE_MAGICSECURE)]	= "magicsecure",
	[const_ilog2(WAKE_FILTER)]	= "filter",
};
static_assert(ARRAY_SIZE(wol_mode_names) == WOL_MODE_COUNT);

const char sof_timestamping_names[][ETH_GSTRING_LEN] = {
	[const_ilog2(SOF_TIMESTAMPING_TX_HARDWARE)]  = "hardware-transmit",
	[const_ilog2(SOF_TIMESTAMPING_TX_SOFTWARE)]  = "software-transmit",
	[const_ilog2(SOF_TIMESTAMPING_RX_HARDWARE)]  = "hardware-receive",
	[const_ilog2(SOF_TIMESTAMPING_RX_SOFTWARE)]  = "software-receive",
	[const_ilog2(SOF_TIMESTAMPING_SOFTWARE)]     = "software-system-clock",
	[const_ilog2(SOF_TIMESTAMPING_SYS_HARDWARE)] = "hardware-legacy-clock",
	[const_ilog2(SOF_TIMESTAMPING_RAW_HARDWARE)] = "hardware-raw-clock",
	[const_ilog2(SOF_TIMESTAMPING_OPT_ID)]       = "option-id",
	[const_ilog2(SOF_TIMESTAMPING_TX_SCHED)]     = "sched-transmit",
	[const_ilog2(SOF_TIMESTAMPING_TX_ACK)]       = "ack-transmit",
	[const_ilog2(SOF_TIMESTAMPING_OPT_CMSG)]     = "option-cmsg",
	[const_ilog2(SOF_TIMESTAMPING_OPT_TSONLY)]   = "option-tsonly",
	[const_ilog2(SOF_TIMESTAMPING_OPT_STATS)]    = "option-stats",
	[const_ilog2(SOF_TIMESTAMPING_OPT_PKTINFO)]  = "option-pktinfo",
	[const_ilog2(SOF_TIMESTAMPING_OPT_TX_SWHW)]  = "option-tx-swhw",
};
static_assert(ARRAY_SIZE(sof_timestamping_names) == __SOF_TIMESTAMPING_CNT);

const char ts_tx_type_names[][ETH_GSTRING_LEN] = {
	[HWTSTAMP_TX_OFF]		= "off",
	[HWTSTAMP_TX_ON]		= "on",
	[HWTSTAMP_TX_ONESTEP_SYNC]	= "onestep-sync",
	[HWTSTAMP_TX_ONESTEP_P2P]	= "onestep-p2p",
};
static_assert(ARRAY_SIZE(ts_tx_type_names) == __HWTSTAMP_TX_CNT);

const char ts_rx_filter_names[][ETH_GSTRING_LEN] = {
	[HWTSTAMP_FILTER_NONE]			= "none",
	[HWTSTAMP_FILTER_ALL]			= "all",
	[HWTSTAMP_FILTER_SOME]			= "some",
	[HWTSTAMP_FILTER_PTP_V1_L4_EVENT]	= "ptpv1-l4-event",
	[HWTSTAMP_FILTER_PTP_V1_L4_SYNC]	= "ptpv1-l4-sync",
	[HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ]	= "ptpv1-l4-delay-req",
	[HWTSTAMP_FILTER_PTP_V2_L4_EVENT]	= "ptpv2-l4-event",
	[HWTSTAMP_FILTER_PTP_V2_L4_SYNC]	= "ptpv2-l4-sync",
	[HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ]	= "ptpv2-l4-delay-req",
	[HWTSTAMP_FILTER_PTP_V2_L2_EVENT]	= "ptpv2-l2-event",
	[HWTSTAMP_FILTER_PTP_V2_L2_SYNC]	= "ptpv2-l2-sync",
	[HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ]	= "ptpv2-l2-delay-req",
	[HWTSTAMP_FILTER_PTP_V2_EVENT]		= "ptpv2-event",
	[HWTSTAMP_FILTER_PTP_V2_SYNC]		= "ptpv2-sync",
	[HWTSTAMP_FILTER_PTP_V2_DELAY_REQ]	= "ptpv2-delay-req",
	[HWTSTAMP_FILTER_NTP_ALL]		= "ntp-all",
};
static_assert(ARRAY_SIZE(ts_rx_filter_names) == __HWTSTAMP_FILTER_CNT);

/* return false if legacy contained non-0 deprecated fields
 * maxtxpkt/maxrxpkt. rest of ksettings always updated
 */
bool
convert_legacy_settings_to_link_ksettings(
	struct ethtool_link_ksettings *link_ksettings,
	const struct ethtool_cmd *legacy_settings)
{
	bool retval = true;

	memset(link_ksettings, 0, sizeof(*link_ksettings));

	/* This is used to tell users that driver is still using these
	 * deprecated legacy fields, and they should not use
	 * %ETHTOOL_GLINKSETTINGS/%ETHTOOL_SLINKSETTINGS
	 */
	if (legacy_settings->maxtxpkt ||
	    legacy_settings->maxrxpkt)
		retval = false;

	ethtool_convert_legacy_u32_to_link_mode(
		link_ksettings->link_modes.supported,
		legacy_settings->supported);
	ethtool_convert_legacy_u32_to_link_mode(
		link_ksettings->link_modes.advertising,
		legacy_settings->advertising);
	ethtool_convert_legacy_u32_to_link_mode(
		link_ksettings->link_modes.lp_advertising,
		legacy_settings->lp_advertising);
	link_ksettings->base.speed
		= ethtool_cmd_speed(legacy_settings);
	link_ksettings->base.duplex
		= legacy_settings->duplex;
	link_ksettings->base.port
		= legacy_settings->port;
	link_ksettings->base.phy_address
		= legacy_settings->phy_address;
	link_ksettings->base.autoneg
		= legacy_settings->autoneg;
	link_ksettings->base.mdio_support
		= legacy_settings->mdio_support;
	link_ksettings->base.eth_tp_mdix
		= legacy_settings->eth_tp_mdix;
	link_ksettings->base.eth_tp_mdix_ctrl
		= legacy_settings->eth_tp_mdix_ctrl;
	return retval;
}

int __ethtool_get_link(struct net_device *dev)
{
	if (!dev->ethtool_ops->get_link)
		return -EOPNOTSUPP;

	return netif_running(dev) && dev->ethtool_ops->get_link(dev);
}

int ethtool_get_max_rxfh_channel(struct net_device *dev, u32 *max)
{
	u32 dev_size, current_max = 0;
	u32 *indir;
	int ret;

	if (!dev->ethtool_ops->get_rxfh_indir_size ||
	    !dev->ethtool_ops->get_rxfh)
		return -EOPNOTSUPP;
	dev_size = dev->ethtool_ops->get_rxfh_indir_size(dev);
	if (dev_size == 0)
		return -EOPNOTSUPP;

	indir = kcalloc(dev_size, sizeof(indir[0]), GFP_USER);
	if (!indir)
		return -ENOMEM;

	ret = dev->ethtool_ops->get_rxfh(dev, indir, NULL, NULL);
	if (ret)
		goto out;

	while (dev_size--)
		current_max = max(current_max, indir[dev_size]);

	*max = current_max;

out:
	kfree(indir);
	return ret;
}

int ethtool_check_ops(const struct ethtool_ops *ops)
{
	if (WARN_ON(ops->set_coalesce && !ops->supported_coalesce_params))
		return -EINVAL;
	/* NOTE: sufficiently insane drivers may swap ethtool_ops at runtime,
	 * the fact that ops are checked at registration time does not
	 * mean the ops attached to a netdev later on are sane.
	 */
	return 0;
}

int __ethtool_get_ts_info(struct net_device *dev, struct ethtool_ts_info *info)
{
	const struct ethtool_ops *ops = dev->ethtool_ops;
	struct phy_device *phydev = dev->phydev;

	memset(info, 0, sizeof(*info));
	info->cmd = ETHTOOL_GET_TS_INFO;

	if (phy_has_tsinfo(phydev))
		return phy_ts_info(phydev, info);
	if (ops->get_ts_info)
		return ops->get_ts_info(dev, info);

	info->so_timestamping = SOF_TIMESTAMPING_RX_SOFTWARE |
				SOF_TIMESTAMPING_SOFTWARE;
	info->phc_index = -1;

	return 0;
}
