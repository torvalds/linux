// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * net/core/ethtool.c - Ethtool ioctl handler
 * Copyright (c) 2003 Matthew Wilcox <matthew@wil.cx>
 *
 * This file is where we call all the ethtool_ops commands to get
 * the information ethtool needs.
 */

#include <linux/compat.h>
#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/net_tstamp.h>
#include <linux/phy.h>
#include <linux/bitops.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/sfp.h>
#include <linux/slab.h>
#include <linux/rtnetlink.h>
#include <linux/sched/signal.h>
#include <linux/net.h>
#include <linux/pm_runtime.h>
#include <net/devlink.h>
#ifndef __GENKSYMS__
#include <net/ipv6.h>
#endif
#include <net/xdp_sock_drv.h>
#include <net/flow_offload.h>
#include <linux/ethtool_netlink.h>
#include <generated/utsrelease.h>
#include "common.h"

/* State held across locks and calls for commands which have devlink fallback */
struct ethtool_devlink_compat {
	struct devlink *devlink;
	union {
		struct ethtool_flash efl;
		struct ethtool_drvinfo info;
	};
};

static struct devlink *netdev_to_devlink_get(struct net_device *dev)
{
	struct devlink_port *devlink_port;

	if (!dev->netdev_ops->ndo_get_devlink_port)
		return NULL;

	devlink_port = dev->netdev_ops->ndo_get_devlink_port(dev);
	if (!devlink_port)
		return NULL;

	return devlink_try_get(devlink_port->devlink);
}

/*
 * Some useful ethtool_ops methods that're device independent.
 * If we find that all drivers want to do the same thing here,
 * we can turn these into dev_() function calls.
 */

u32 ethtool_op_get_link(struct net_device *dev)
{
	return netif_carrier_ok(dev) ? 1 : 0;
}
EXPORT_SYMBOL(ethtool_op_get_link);

int ethtool_op_get_ts_info(struct net_device *dev, struct ethtool_ts_info *info)
{
	info->so_timestamping =
		SOF_TIMESTAMPING_TX_SOFTWARE |
		SOF_TIMESTAMPING_RX_SOFTWARE |
		SOF_TIMESTAMPING_SOFTWARE;
	info->phc_index = -1;
	return 0;
}
EXPORT_SYMBOL(ethtool_op_get_ts_info);

/* Handlers for each ethtool command */

static int ethtool_get_features(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_gfeatures cmd = {
		.cmd = ETHTOOL_GFEATURES,
		.size = ETHTOOL_DEV_FEATURE_WORDS,
	};
	struct ethtool_get_features_block features[ETHTOOL_DEV_FEATURE_WORDS];
	u32 __user *sizeaddr;
	u32 copy_size;
	int i;

	/* in case feature bits run out again */
	BUILD_BUG_ON(ETHTOOL_DEV_FEATURE_WORDS * sizeof(u32) > sizeof(netdev_features_t));

	for (i = 0; i < ETHTOOL_DEV_FEATURE_WORDS; ++i) {
		features[i].available = (u32)(dev->hw_features >> (32 * i));
		features[i].requested = (u32)(dev->wanted_features >> (32 * i));
		features[i].active = (u32)(dev->features >> (32 * i));
		features[i].never_changed =
			(u32)(NETIF_F_NEVER_CHANGE >> (32 * i));
	}

	sizeaddr = useraddr + offsetof(struct ethtool_gfeatures, size);
	if (get_user(copy_size, sizeaddr))
		return -EFAULT;

	if (copy_size > ETHTOOL_DEV_FEATURE_WORDS)
		copy_size = ETHTOOL_DEV_FEATURE_WORDS;

	if (copy_to_user(useraddr, &cmd, sizeof(cmd)))
		return -EFAULT;
	useraddr += sizeof(cmd);
	if (copy_to_user(useraddr, features,
			 array_size(copy_size, sizeof(*features))))
		return -EFAULT;

	return 0;
}

static int ethtool_set_features(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_sfeatures cmd;
	struct ethtool_set_features_block features[ETHTOOL_DEV_FEATURE_WORDS];
	netdev_features_t wanted = 0, valid = 0;
	int i, ret = 0;

	if (copy_from_user(&cmd, useraddr, sizeof(cmd)))
		return -EFAULT;
	useraddr += sizeof(cmd);

	if (cmd.size != ETHTOOL_DEV_FEATURE_WORDS)
		return -EINVAL;

	if (copy_from_user(features, useraddr, sizeof(features)))
		return -EFAULT;

	for (i = 0; i < ETHTOOL_DEV_FEATURE_WORDS; ++i) {
		valid |= (netdev_features_t)features[i].valid << (32 * i);
		wanted |= (netdev_features_t)features[i].requested << (32 * i);
	}

	if (valid & ~NETIF_F_ETHTOOL_BITS)
		return -EINVAL;

	if (valid & ~dev->hw_features) {
		valid &= dev->hw_features;
		ret |= ETHTOOL_F_UNSUPPORTED;
	}

	dev->wanted_features &= ~valid;
	dev->wanted_features |= wanted & valid;
	__netdev_update_features(dev);

	if ((dev->wanted_features ^ dev->features) & valid)
		ret |= ETHTOOL_F_WISH;

	return ret;
}

static int __ethtool_get_sset_count(struct net_device *dev, int sset)
{
	const struct ethtool_phy_ops *phy_ops = ethtool_phy_ops;
	const struct ethtool_ops *ops = dev->ethtool_ops;

	if (sset == ETH_SS_FEATURES)
		return ARRAY_SIZE(netdev_features_strings);

	if (sset == ETH_SS_RSS_HASH_FUNCS)
		return ARRAY_SIZE(rss_hash_func_strings);

	if (sset == ETH_SS_TUNABLES)
		return ARRAY_SIZE(tunable_strings);

	if (sset == ETH_SS_PHY_TUNABLES)
		return ARRAY_SIZE(phy_tunable_strings);

	if (sset == ETH_SS_PHY_STATS && dev->phydev &&
	    !ops->get_ethtool_phy_stats &&
	    phy_ops && phy_ops->get_sset_count)
		return phy_ops->get_sset_count(dev->phydev);

	if (sset == ETH_SS_LINK_MODES)
		return __ETHTOOL_LINK_MODE_MASK_NBITS;

	if (ops->get_sset_count && ops->get_strings)
		return ops->get_sset_count(dev, sset);
	else
		return -EOPNOTSUPP;
}

static void __ethtool_get_strings(struct net_device *dev,
	u32 stringset, u8 *data)
{
	const struct ethtool_phy_ops *phy_ops = ethtool_phy_ops;
	const struct ethtool_ops *ops = dev->ethtool_ops;

	if (stringset == ETH_SS_FEATURES)
		memcpy(data, netdev_features_strings,
			sizeof(netdev_features_strings));
	else if (stringset == ETH_SS_RSS_HASH_FUNCS)
		memcpy(data, rss_hash_func_strings,
		       sizeof(rss_hash_func_strings));
	else if (stringset == ETH_SS_TUNABLES)
		memcpy(data, tunable_strings, sizeof(tunable_strings));
	else if (stringset == ETH_SS_PHY_TUNABLES)
		memcpy(data, phy_tunable_strings, sizeof(phy_tunable_strings));
	else if (stringset == ETH_SS_PHY_STATS && dev->phydev &&
		 !ops->get_ethtool_phy_stats && phy_ops &&
		 phy_ops->get_strings)
		phy_ops->get_strings(dev->phydev, data);
	else if (stringset == ETH_SS_LINK_MODES)
		memcpy(data, link_mode_names,
		       __ETHTOOL_LINK_MODE_MASK_NBITS * ETH_GSTRING_LEN);
	else
		/* ops->get_strings is valid because checked earlier */
		ops->get_strings(dev, stringset, data);
}

static netdev_features_t ethtool_get_feature_mask(u32 eth_cmd)
{
	/* feature masks of legacy discrete ethtool ops */

	switch (eth_cmd) {
	case ETHTOOL_GTXCSUM:
	case ETHTOOL_STXCSUM:
		return NETIF_F_CSUM_MASK | NETIF_F_FCOE_CRC |
		       NETIF_F_SCTP_CRC;
	case ETHTOOL_GRXCSUM:
	case ETHTOOL_SRXCSUM:
		return NETIF_F_RXCSUM;
	case ETHTOOL_GSG:
	case ETHTOOL_SSG:
		return NETIF_F_SG | NETIF_F_FRAGLIST;
	case ETHTOOL_GTSO:
	case ETHTOOL_STSO:
		return NETIF_F_ALL_TSO;
	case ETHTOOL_GGSO:
	case ETHTOOL_SGSO:
		return NETIF_F_GSO;
	case ETHTOOL_GGRO:
	case ETHTOOL_SGRO:
		return NETIF_F_GRO;
	default:
		BUG();
	}
}

static int ethtool_get_one_feature(struct net_device *dev,
	char __user *useraddr, u32 ethcmd)
{
	netdev_features_t mask = ethtool_get_feature_mask(ethcmd);
	struct ethtool_value edata = {
		.cmd = ethcmd,
		.data = !!(dev->features & mask),
	};

	if (copy_to_user(useraddr, &edata, sizeof(edata)))
		return -EFAULT;
	return 0;
}

static int ethtool_set_one_feature(struct net_device *dev,
	void __user *useraddr, u32 ethcmd)
{
	struct ethtool_value edata;
	netdev_features_t mask;

	if (copy_from_user(&edata, useraddr, sizeof(edata)))
		return -EFAULT;

	mask = ethtool_get_feature_mask(ethcmd);
	mask &= dev->hw_features;
	if (!mask)
		return -EOPNOTSUPP;

	if (edata.data)
		dev->wanted_features |= mask;
	else
		dev->wanted_features &= ~mask;

	__netdev_update_features(dev);

	return 0;
}

#define ETH_ALL_FLAGS    (ETH_FLAG_LRO | ETH_FLAG_RXVLAN | ETH_FLAG_TXVLAN | \
			  ETH_FLAG_NTUPLE | ETH_FLAG_RXHASH)
#define ETH_ALL_FEATURES (NETIF_F_LRO | NETIF_F_HW_VLAN_CTAG_RX | \
			  NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_NTUPLE | \
			  NETIF_F_RXHASH)

static u32 __ethtool_get_flags(struct net_device *dev)
{
	u32 flags = 0;

	if (dev->features & NETIF_F_LRO)
		flags |= ETH_FLAG_LRO;
	if (dev->features & NETIF_F_HW_VLAN_CTAG_RX)
		flags |= ETH_FLAG_RXVLAN;
	if (dev->features & NETIF_F_HW_VLAN_CTAG_TX)
		flags |= ETH_FLAG_TXVLAN;
	if (dev->features & NETIF_F_NTUPLE)
		flags |= ETH_FLAG_NTUPLE;
	if (dev->features & NETIF_F_RXHASH)
		flags |= ETH_FLAG_RXHASH;

	return flags;
}

static int __ethtool_set_flags(struct net_device *dev, u32 data)
{
	netdev_features_t features = 0, changed;

	if (data & ~ETH_ALL_FLAGS)
		return -EINVAL;

	if (data & ETH_FLAG_LRO)
		features |= NETIF_F_LRO;
	if (data & ETH_FLAG_RXVLAN)
		features |= NETIF_F_HW_VLAN_CTAG_RX;
	if (data & ETH_FLAG_TXVLAN)
		features |= NETIF_F_HW_VLAN_CTAG_TX;
	if (data & ETH_FLAG_NTUPLE)
		features |= NETIF_F_NTUPLE;
	if (data & ETH_FLAG_RXHASH)
		features |= NETIF_F_RXHASH;

	/* allow changing only bits set in hw_features */
	changed = (features ^ dev->features) & ETH_ALL_FEATURES;
	if (changed & ~dev->hw_features)
		return (changed & dev->hw_features) ? -EINVAL : -EOPNOTSUPP;

	dev->wanted_features =
		(dev->wanted_features & ~changed) | (features & changed);

	__netdev_update_features(dev);

	return 0;
}

/* Given two link masks, AND them together and save the result in dst. */
void ethtool_intersect_link_masks(struct ethtool_link_ksettings *dst,
				  struct ethtool_link_ksettings *src)
{
	unsigned int size = BITS_TO_LONGS(__ETHTOOL_LINK_MODE_MASK_NBITS);
	unsigned int idx = 0;

	for (; idx < size; idx++) {
		dst->link_modes.supported[idx] &=
			src->link_modes.supported[idx];
		dst->link_modes.advertising[idx] &=
			src->link_modes.advertising[idx];
	}
}
EXPORT_SYMBOL(ethtool_intersect_link_masks);

void ethtool_convert_legacy_u32_to_link_mode(unsigned long *dst,
					     u32 legacy_u32)
{
	linkmode_zero(dst);
	dst[0] = legacy_u32;
}
EXPORT_SYMBOL(ethtool_convert_legacy_u32_to_link_mode);

/* return false if src had higher bits set. lower bits always updated. */
bool ethtool_convert_link_mode_to_legacy_u32(u32 *legacy_u32,
					     const unsigned long *src)
{
	*legacy_u32 = src[0];
	return find_next_bit(src, __ETHTOOL_LINK_MODE_MASK_NBITS, 32) ==
		__ETHTOOL_LINK_MODE_MASK_NBITS;
}
EXPORT_SYMBOL(ethtool_convert_link_mode_to_legacy_u32);

/* return false if ksettings link modes had higher bits
 * set. legacy_settings always updated (best effort)
 */
static bool
convert_link_ksettings_to_legacy_settings(
	struct ethtool_cmd *legacy_settings,
	const struct ethtool_link_ksettings *link_ksettings)
{
	bool retval = true;

	memset(legacy_settings, 0, sizeof(*legacy_settings));
	/* this also clears the deprecated fields in legacy structure:
	 * __u8		transceiver;
	 * __u32	maxtxpkt;
	 * __u32	maxrxpkt;
	 */

	retval &= ethtool_convert_link_mode_to_legacy_u32(
		&legacy_settings->supported,
		link_ksettings->link_modes.supported);
	retval &= ethtool_convert_link_mode_to_legacy_u32(
		&legacy_settings->advertising,
		link_ksettings->link_modes.advertising);
	retval &= ethtool_convert_link_mode_to_legacy_u32(
		&legacy_settings->lp_advertising,
		link_ksettings->link_modes.lp_advertising);
	ethtool_cmd_speed_set(legacy_settings, link_ksettings->base.speed);
	legacy_settings->duplex
		= link_ksettings->base.duplex;
	legacy_settings->port
		= link_ksettings->base.port;
	legacy_settings->phy_address
		= link_ksettings->base.phy_address;
	legacy_settings->autoneg
		= link_ksettings->base.autoneg;
	legacy_settings->mdio_support
		= link_ksettings->base.mdio_support;
	legacy_settings->eth_tp_mdix
		= link_ksettings->base.eth_tp_mdix;
	legacy_settings->eth_tp_mdix_ctrl
		= link_ksettings->base.eth_tp_mdix_ctrl;
	legacy_settings->transceiver
		= link_ksettings->base.transceiver;
	return retval;
}

/* number of 32-bit words to store the user's link mode bitmaps */
#define __ETHTOOL_LINK_MODE_MASK_NU32			\
	DIV_ROUND_UP(__ETHTOOL_LINK_MODE_MASK_NBITS, 32)

/* layout of the struct passed from/to userland */
struct ethtool_link_usettings {
	struct ethtool_link_settings base;
	struct {
		__u32 supported[__ETHTOOL_LINK_MODE_MASK_NU32];
		__u32 advertising[__ETHTOOL_LINK_MODE_MASK_NU32];
		__u32 lp_advertising[__ETHTOOL_LINK_MODE_MASK_NU32];
	} link_modes;
};

/* Internal kernel helper to query a device ethtool_link_settings. */
int __ethtool_get_link_ksettings(struct net_device *dev,
				 struct ethtool_link_ksettings *link_ksettings)
{
	ASSERT_RTNL();

	if (!dev->ethtool_ops->get_link_ksettings)
		return -EOPNOTSUPP;

	memset(link_ksettings, 0, sizeof(*link_ksettings));
	return dev->ethtool_ops->get_link_ksettings(dev, link_ksettings);
}
EXPORT_SYMBOL(__ethtool_get_link_ksettings);

/* convert ethtool_link_usettings in user space to a kernel internal
 * ethtool_link_ksettings. return 0 on success, errno on error.
 */
static int load_link_ksettings_from_user(struct ethtool_link_ksettings *to,
					 const void __user *from)
{
	struct ethtool_link_usettings link_usettings;

	if (copy_from_user(&link_usettings, from, sizeof(link_usettings)))
		return -EFAULT;

	memcpy(&to->base, &link_usettings.base, sizeof(to->base));
	bitmap_from_arr32(to->link_modes.supported,
			  link_usettings.link_modes.supported,
			  __ETHTOOL_LINK_MODE_MASK_NBITS);
	bitmap_from_arr32(to->link_modes.advertising,
			  link_usettings.link_modes.advertising,
			  __ETHTOOL_LINK_MODE_MASK_NBITS);
	bitmap_from_arr32(to->link_modes.lp_advertising,
			  link_usettings.link_modes.lp_advertising,
			  __ETHTOOL_LINK_MODE_MASK_NBITS);

	return 0;
}

/* Check if the user is trying to change anything besides speed/duplex */
bool ethtool_virtdev_validate_cmd(const struct ethtool_link_ksettings *cmd)
{
	struct ethtool_link_settings base2 = {};

	base2.speed = cmd->base.speed;
	base2.port = PORT_OTHER;
	base2.duplex = cmd->base.duplex;
	base2.cmd = cmd->base.cmd;
	base2.link_mode_masks_nwords = cmd->base.link_mode_masks_nwords;

	return !memcmp(&base2, &cmd->base, sizeof(base2)) &&
		bitmap_empty(cmd->link_modes.supported,
			     __ETHTOOL_LINK_MODE_MASK_NBITS) &&
		bitmap_empty(cmd->link_modes.lp_advertising,
			     __ETHTOOL_LINK_MODE_MASK_NBITS);
}

/* convert a kernel internal ethtool_link_ksettings to
 * ethtool_link_usettings in user space. return 0 on success, errno on
 * error.
 */
static int
store_link_ksettings_for_user(void __user *to,
			      const struct ethtool_link_ksettings *from)
{
	struct ethtool_link_usettings link_usettings;

	memcpy(&link_usettings, from, sizeof(link_usettings));
	bitmap_to_arr32(link_usettings.link_modes.supported,
			from->link_modes.supported,
			__ETHTOOL_LINK_MODE_MASK_NBITS);
	bitmap_to_arr32(link_usettings.link_modes.advertising,
			from->link_modes.advertising,
			__ETHTOOL_LINK_MODE_MASK_NBITS);
	bitmap_to_arr32(link_usettings.link_modes.lp_advertising,
			from->link_modes.lp_advertising,
			__ETHTOOL_LINK_MODE_MASK_NBITS);

	if (copy_to_user(to, &link_usettings, sizeof(link_usettings)))
		return -EFAULT;

	return 0;
}

/* Query device for its ethtool_link_settings. */
static int ethtool_get_link_ksettings(struct net_device *dev,
				      void __user *useraddr)
{
	int err = 0;
	struct ethtool_link_ksettings link_ksettings;

	ASSERT_RTNL();
	if (!dev->ethtool_ops->get_link_ksettings)
		return -EOPNOTSUPP;

	/* handle bitmap nbits handshake */
	if (copy_from_user(&link_ksettings.base, useraddr,
			   sizeof(link_ksettings.base)))
		return -EFAULT;

	if (__ETHTOOL_LINK_MODE_MASK_NU32
	    != link_ksettings.base.link_mode_masks_nwords) {
		/* wrong link mode nbits requested */
		memset(&link_ksettings, 0, sizeof(link_ksettings));
		link_ksettings.base.cmd = ETHTOOL_GLINKSETTINGS;
		/* send back number of words required as negative val */
		compiletime_assert(__ETHTOOL_LINK_MODE_MASK_NU32 <= S8_MAX,
				   "need too many bits for link modes!");
		link_ksettings.base.link_mode_masks_nwords
			= -((s8)__ETHTOOL_LINK_MODE_MASK_NU32);

		/* copy the base fields back to user, not the link
		 * mode bitmaps
		 */
		if (copy_to_user(useraddr, &link_ksettings.base,
				 sizeof(link_ksettings.base)))
			return -EFAULT;

		return 0;
	}

	/* handshake successful: user/kernel agree on
	 * link_mode_masks_nwords
	 */

	memset(&link_ksettings, 0, sizeof(link_ksettings));
	err = dev->ethtool_ops->get_link_ksettings(dev, &link_ksettings);
	if (err < 0)
		return err;

	/* make sure we tell the right values to user */
	link_ksettings.base.cmd = ETHTOOL_GLINKSETTINGS;
	link_ksettings.base.link_mode_masks_nwords
		= __ETHTOOL_LINK_MODE_MASK_NU32;
	link_ksettings.base.master_slave_cfg = MASTER_SLAVE_CFG_UNSUPPORTED;
	link_ksettings.base.master_slave_state = MASTER_SLAVE_STATE_UNSUPPORTED;
	link_ksettings.base.rate_matching = RATE_MATCH_NONE;

	return store_link_ksettings_for_user(useraddr, &link_ksettings);
}

/* Update device ethtool_link_settings. */
static int ethtool_set_link_ksettings(struct net_device *dev,
				      void __user *useraddr)
{
	struct ethtool_link_ksettings link_ksettings = {};
	int err;

	ASSERT_RTNL();

	if (!dev->ethtool_ops->set_link_ksettings)
		return -EOPNOTSUPP;

	/* make sure nbits field has expected value */
	if (copy_from_user(&link_ksettings.base, useraddr,
			   sizeof(link_ksettings.base)))
		return -EFAULT;

	if (__ETHTOOL_LINK_MODE_MASK_NU32
	    != link_ksettings.base.link_mode_masks_nwords)
		return -EINVAL;

	/* copy the whole structure, now that we know it has expected
	 * format
	 */
	err = load_link_ksettings_from_user(&link_ksettings, useraddr);
	if (err)
		return err;

	/* re-check nwords field, just in case */
	if (__ETHTOOL_LINK_MODE_MASK_NU32
	    != link_ksettings.base.link_mode_masks_nwords)
		return -EINVAL;

	if (link_ksettings.base.master_slave_cfg ||
	    link_ksettings.base.master_slave_state)
		return -EINVAL;

	err = dev->ethtool_ops->set_link_ksettings(dev, &link_ksettings);
	if (err >= 0) {
		ethtool_notify(dev, ETHTOOL_MSG_LINKINFO_NTF, NULL);
		ethtool_notify(dev, ETHTOOL_MSG_LINKMODES_NTF, NULL);
	}
	return err;
}

int ethtool_virtdev_set_link_ksettings(struct net_device *dev,
				       const struct ethtool_link_ksettings *cmd,
				       u32 *dev_speed, u8 *dev_duplex)
{
	u32 speed;
	u8 duplex;

	speed = cmd->base.speed;
	duplex = cmd->base.duplex;
	/* don't allow custom speed and duplex */
	if (!ethtool_validate_speed(speed) ||
	    !ethtool_validate_duplex(duplex) ||
	    !ethtool_virtdev_validate_cmd(cmd))
		return -EINVAL;
	*dev_speed = speed;
	*dev_duplex = duplex;

	return 0;
}
EXPORT_SYMBOL(ethtool_virtdev_set_link_ksettings);

/* Query device for its ethtool_cmd settings.
 *
 * Backward compatibility note: for compatibility with legacy ethtool, this is
 * now implemented via get_link_ksettings. When driver reports higher link mode
 * bits, a kernel warning is logged once (with name of 1st driver/device) to
 * recommend user to upgrade ethtool, but the command is successful (only the
 * lower link mode bits reported back to user). Deprecated fields from
 * ethtool_cmd (transceiver/maxrxpkt/maxtxpkt) are always set to zero.
 */
static int ethtool_get_settings(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_link_ksettings link_ksettings;
	struct ethtool_cmd cmd;
	int err;

	ASSERT_RTNL();
	if (!dev->ethtool_ops->get_link_ksettings)
		return -EOPNOTSUPP;

	memset(&link_ksettings, 0, sizeof(link_ksettings));
	err = dev->ethtool_ops->get_link_ksettings(dev, &link_ksettings);
	if (err < 0)
		return err;
	convert_link_ksettings_to_legacy_settings(&cmd, &link_ksettings);

	/* send a sensible cmd tag back to user */
	cmd.cmd = ETHTOOL_GSET;

	if (copy_to_user(useraddr, &cmd, sizeof(cmd)))
		return -EFAULT;

	return 0;
}

/* Update device link settings with given ethtool_cmd.
 *
 * Backward compatibility note: for compatibility with legacy ethtool, this is
 * now always implemented via set_link_settings. When user's request updates
 * deprecated ethtool_cmd fields (transceiver/maxrxpkt/maxtxpkt), a kernel
 * warning is logged once (with name of 1st driver/device) to recommend user to
 * upgrade ethtool, and the request is rejected.
 */
static int ethtool_set_settings(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_link_ksettings link_ksettings;
	struct ethtool_cmd cmd;
	int ret;

	ASSERT_RTNL();

	if (copy_from_user(&cmd, useraddr, sizeof(cmd)))
		return -EFAULT;
	if (!dev->ethtool_ops->set_link_ksettings)
		return -EOPNOTSUPP;

	if (!convert_legacy_settings_to_link_ksettings(&link_ksettings, &cmd))
		return -EINVAL;
	link_ksettings.base.link_mode_masks_nwords =
		__ETHTOOL_LINK_MODE_MASK_NU32;
	ret = dev->ethtool_ops->set_link_ksettings(dev, &link_ksettings);
	if (ret >= 0) {
		ethtool_notify(dev, ETHTOOL_MSG_LINKINFO_NTF, NULL);
		ethtool_notify(dev, ETHTOOL_MSG_LINKMODES_NTF, NULL);
	}
	return ret;
}

static int
ethtool_get_drvinfo(struct net_device *dev, struct ethtool_devlink_compat *rsp)
{
	const struct ethtool_ops *ops = dev->ethtool_ops;

	rsp->info.cmd = ETHTOOL_GDRVINFO;
	strscpy(rsp->info.version, UTS_RELEASE, sizeof(rsp->info.version));
	if (ops->get_drvinfo) {
		ops->get_drvinfo(dev, &rsp->info);
	} else if (dev->dev.parent && dev->dev.parent->driver) {
		strscpy(rsp->info.bus_info, dev_name(dev->dev.parent),
			sizeof(rsp->info.bus_info));
		strscpy(rsp->info.driver, dev->dev.parent->driver->name,
			sizeof(rsp->info.driver));
	} else if (dev->rtnl_link_ops) {
		strscpy(rsp->info.driver, dev->rtnl_link_ops->kind,
			sizeof(rsp->info.driver));
	} else {
		return -EOPNOTSUPP;
	}

	/*
	 * this method of obtaining string set info is deprecated;
	 * Use ETHTOOL_GSSET_INFO instead.
	 */
	if (ops->get_sset_count) {
		int rc;

		rc = ops->get_sset_count(dev, ETH_SS_TEST);
		if (rc >= 0)
			rsp->info.testinfo_len = rc;
		rc = ops->get_sset_count(dev, ETH_SS_STATS);
		if (rc >= 0)
			rsp->info.n_stats = rc;
		rc = ops->get_sset_count(dev, ETH_SS_PRIV_FLAGS);
		if (rc >= 0)
			rsp->info.n_priv_flags = rc;
	}
	if (ops->get_regs_len) {
		int ret = ops->get_regs_len(dev);

		if (ret > 0)
			rsp->info.regdump_len = ret;
	}

	if (ops->get_eeprom_len)
		rsp->info.eedump_len = ops->get_eeprom_len(dev);

	if (!rsp->info.fw_version[0])
		rsp->devlink = netdev_to_devlink_get(dev);

	return 0;
}

static noinline_for_stack int ethtool_get_sset_info(struct net_device *dev,
						    void __user *useraddr)
{
	struct ethtool_sset_info info;
	u64 sset_mask;
	int i, idx = 0, n_bits = 0, ret, rc;
	u32 *info_buf = NULL;

	if (copy_from_user(&info, useraddr, sizeof(info)))
		return -EFAULT;

	/* store copy of mask, because we zero struct later on */
	sset_mask = info.sset_mask;
	if (!sset_mask)
		return 0;

	/* calculate size of return buffer */
	n_bits = hweight64(sset_mask);

	memset(&info, 0, sizeof(info));
	info.cmd = ETHTOOL_GSSET_INFO;

	info_buf = kcalloc(n_bits, sizeof(u32), GFP_USER);
	if (!info_buf)
		return -ENOMEM;

	/*
	 * fill return buffer based on input bitmask and successful
	 * get_sset_count return
	 */
	for (i = 0; i < 64; i++) {
		if (!(sset_mask & (1ULL << i)))
			continue;

		rc = __ethtool_get_sset_count(dev, i);
		if (rc >= 0) {
			info.sset_mask |= (1ULL << i);
			info_buf[idx++] = rc;
		}
	}

	ret = -EFAULT;
	if (copy_to_user(useraddr, &info, sizeof(info)))
		goto out;

	useraddr += offsetof(struct ethtool_sset_info, data);
	if (copy_to_user(useraddr, info_buf, array_size(idx, sizeof(u32))))
		goto out;

	ret = 0;

out:
	kfree(info_buf);
	return ret;
}

static noinline_for_stack int
ethtool_rxnfc_copy_from_compat(struct ethtool_rxnfc *rxnfc,
			       const struct compat_ethtool_rxnfc __user *useraddr,
			       size_t size)
{
	struct compat_ethtool_rxnfc crxnfc = {};

	/* We expect there to be holes between fs.m_ext and
	 * fs.ring_cookie and at the end of fs, but nowhere else.
	 * On non-x86, no conversion should be needed.
	 */
	BUILD_BUG_ON(!IS_ENABLED(CONFIG_X86_64) &&
		     sizeof(struct compat_ethtool_rxnfc) !=
		     sizeof(struct ethtool_rxnfc));
	BUILD_BUG_ON(offsetof(struct compat_ethtool_rxnfc, fs.m_ext) +
		     sizeof(useraddr->fs.m_ext) !=
		     offsetof(struct ethtool_rxnfc, fs.m_ext) +
		     sizeof(rxnfc->fs.m_ext));
	BUILD_BUG_ON(offsetof(struct compat_ethtool_rxnfc, fs.location) -
		     offsetof(struct compat_ethtool_rxnfc, fs.ring_cookie) !=
		     offsetof(struct ethtool_rxnfc, fs.location) -
		     offsetof(struct ethtool_rxnfc, fs.ring_cookie));

	if (copy_from_user(&crxnfc, useraddr, min(size, sizeof(crxnfc))))
		return -EFAULT;

	*rxnfc = (struct ethtool_rxnfc) {
		.cmd		= crxnfc.cmd,
		.flow_type	= crxnfc.flow_type,
		.data		= crxnfc.data,
		.fs		= {
			.flow_type	= crxnfc.fs.flow_type,
			.h_u		= crxnfc.fs.h_u,
			.h_ext		= crxnfc.fs.h_ext,
			.m_u		= crxnfc.fs.m_u,
			.m_ext		= crxnfc.fs.m_ext,
			.ring_cookie	= crxnfc.fs.ring_cookie,
			.location	= crxnfc.fs.location,
		},
		.rule_cnt	= crxnfc.rule_cnt,
	};

	return 0;
}

static int ethtool_rxnfc_copy_from_user(struct ethtool_rxnfc *rxnfc,
					const void __user *useraddr,
					size_t size)
{
	if (compat_need_64bit_alignment_fixup())
		return ethtool_rxnfc_copy_from_compat(rxnfc, useraddr, size);

	if (copy_from_user(rxnfc, useraddr, size))
		return -EFAULT;

	return 0;
}

static int ethtool_rxnfc_copy_to_compat(void __user *useraddr,
					const struct ethtool_rxnfc *rxnfc,
					size_t size, const u32 *rule_buf)
{
	struct compat_ethtool_rxnfc crxnfc;

	memset(&crxnfc, 0, sizeof(crxnfc));
	crxnfc = (struct compat_ethtool_rxnfc) {
		.cmd		= rxnfc->cmd,
		.flow_type	= rxnfc->flow_type,
		.data		= rxnfc->data,
		.fs		= {
			.flow_type	= rxnfc->fs.flow_type,
			.h_u		= rxnfc->fs.h_u,
			.h_ext		= rxnfc->fs.h_ext,
			.m_u		= rxnfc->fs.m_u,
			.m_ext		= rxnfc->fs.m_ext,
			.ring_cookie	= rxnfc->fs.ring_cookie,
			.location	= rxnfc->fs.location,
		},
		.rule_cnt	= rxnfc->rule_cnt,
	};

	if (copy_to_user(useraddr, &crxnfc, min(size, sizeof(crxnfc))))
		return -EFAULT;

	return 0;
}

static int ethtool_rxnfc_copy_to_user(void __user *useraddr,
				      const struct ethtool_rxnfc *rxnfc,
				      size_t size, const u32 *rule_buf)
{
	int ret;

	if (compat_need_64bit_alignment_fixup()) {
		ret = ethtool_rxnfc_copy_to_compat(useraddr, rxnfc, size,
						   rule_buf);
		useraddr += offsetof(struct compat_ethtool_rxnfc, rule_locs);
	} else {
		ret = copy_to_user(useraddr, rxnfc, size);
		useraddr += offsetof(struct ethtool_rxnfc, rule_locs);
	}

	if (ret)
		return -EFAULT;

	if (rule_buf) {
		if (copy_to_user(useraddr, rule_buf,
				 rxnfc->rule_cnt * sizeof(u32)))
			return -EFAULT;
	}

	return 0;
}

static noinline_for_stack int ethtool_set_rxnfc(struct net_device *dev,
						u32 cmd, void __user *useraddr)
{
	struct ethtool_rxnfc info;
	size_t info_size = sizeof(info);
	int rc;

	if (!dev->ethtool_ops->set_rxnfc)
		return -EOPNOTSUPP;

	/* struct ethtool_rxnfc was originally defined for
	 * ETHTOOL_{G,S}RXFH with only the cmd, flow_type and data
	 * members.  User-space might still be using that
	 * definition. */
	if (cmd == ETHTOOL_SRXFH)
		info_size = (offsetof(struct ethtool_rxnfc, data) +
			     sizeof(info.data));

	if (ethtool_rxnfc_copy_from_user(&info, useraddr, info_size))
		return -EFAULT;

	rc = dev->ethtool_ops->set_rxnfc(dev, &info);
	if (rc)
		return rc;

	if (cmd == ETHTOOL_SRXCLSRLINS &&
	    ethtool_rxnfc_copy_to_user(useraddr, &info, info_size, NULL))
		return -EFAULT;

	return 0;
}

static noinline_for_stack int ethtool_get_rxnfc(struct net_device *dev,
						u32 cmd, void __user *useraddr)
{
	struct ethtool_rxnfc info;
	size_t info_size = sizeof(info);
	const struct ethtool_ops *ops = dev->ethtool_ops;
	int ret;
	void *rule_buf = NULL;

	if (!ops->get_rxnfc)
		return -EOPNOTSUPP;

	/* struct ethtool_rxnfc was originally defined for
	 * ETHTOOL_{G,S}RXFH with only the cmd, flow_type and data
	 * members.  User-space might still be using that
	 * definition. */
	if (cmd == ETHTOOL_GRXFH)
		info_size = (offsetof(struct ethtool_rxnfc, data) +
			     sizeof(info.data));

	if (ethtool_rxnfc_copy_from_user(&info, useraddr, info_size))
		return -EFAULT;

	/* If FLOW_RSS was requested then user-space must be using the
	 * new definition, as FLOW_RSS is newer.
	 */
	if (cmd == ETHTOOL_GRXFH && info.flow_type & FLOW_RSS) {
		info_size = sizeof(info);
		if (ethtool_rxnfc_copy_from_user(&info, useraddr, info_size))
			return -EFAULT;
		/* Since malicious users may modify the original data,
		 * we need to check whether FLOW_RSS is still requested.
		 */
		if (!(info.flow_type & FLOW_RSS))
			return -EINVAL;
	}

	if (info.cmd != cmd)
		return -EINVAL;

	if (info.cmd == ETHTOOL_GRXCLSRLALL) {
		if (info.rule_cnt > 0) {
			if (info.rule_cnt <= KMALLOC_MAX_SIZE / sizeof(u32))
				rule_buf = kcalloc(info.rule_cnt, sizeof(u32),
						   GFP_USER);
			if (!rule_buf)
				return -ENOMEM;
		}
	}

	ret = ops->get_rxnfc(dev, &info, rule_buf);
	if (ret < 0)
		goto err_out;

	ret = ethtool_rxnfc_copy_to_user(useraddr, &info, info_size, rule_buf);
err_out:
	kfree(rule_buf);

	return ret;
}

static int ethtool_copy_validate_indir(u32 *indir, void __user *useraddr,
					struct ethtool_rxnfc *rx_rings,
					u32 size)
{
	int i;

	if (copy_from_user(indir, useraddr, array_size(size, sizeof(indir[0]))))
		return -EFAULT;

	/* Validate ring indices */
	for (i = 0; i < size; i++)
		if (indir[i] >= rx_rings->data)
			return -EINVAL;

	return 0;
}

u8 netdev_rss_key[NETDEV_RSS_KEY_LEN] __read_mostly;

void netdev_rss_key_fill(void *buffer, size_t len)
{
	BUG_ON(len > sizeof(netdev_rss_key));
	net_get_random_once(netdev_rss_key, sizeof(netdev_rss_key));
	memcpy(buffer, netdev_rss_key, len);
}
EXPORT_SYMBOL(netdev_rss_key_fill);

static noinline_for_stack int ethtool_get_rxfh_indir(struct net_device *dev,
						     void __user *useraddr)
{
	u32 user_size, dev_size;
	u32 *indir;
	int ret;

	if (!dev->ethtool_ops->get_rxfh_indir_size ||
	    !dev->ethtool_ops->get_rxfh)
		return -EOPNOTSUPP;
	dev_size = dev->ethtool_ops->get_rxfh_indir_size(dev);
	if (dev_size == 0)
		return -EOPNOTSUPP;

	if (copy_from_user(&user_size,
			   useraddr + offsetof(struct ethtool_rxfh_indir, size),
			   sizeof(user_size)))
		return -EFAULT;

	if (copy_to_user(useraddr + offsetof(struct ethtool_rxfh_indir, size),
			 &dev_size, sizeof(dev_size)))
		return -EFAULT;

	/* If the user buffer size is 0, this is just a query for the
	 * device table size.  Otherwise, if it's smaller than the
	 * device table size it's an error.
	 */
	if (user_size < dev_size)
		return user_size == 0 ? 0 : -EINVAL;

	indir = kcalloc(dev_size, sizeof(indir[0]), GFP_USER);
	if (!indir)
		return -ENOMEM;

	ret = dev->ethtool_ops->get_rxfh(dev, indir, NULL, NULL);
	if (ret)
		goto out;

	if (copy_to_user(useraddr +
			 offsetof(struct ethtool_rxfh_indir, ring_index[0]),
			 indir, dev_size * sizeof(indir[0])))
		ret = -EFAULT;

out:
	kfree(indir);
	return ret;
}

static noinline_for_stack int ethtool_set_rxfh_indir(struct net_device *dev,
						     void __user *useraddr)
{
	struct ethtool_rxnfc rx_rings;
	u32 user_size, dev_size, i;
	u32 *indir;
	const struct ethtool_ops *ops = dev->ethtool_ops;
	int ret;
	u32 ringidx_offset = offsetof(struct ethtool_rxfh_indir, ring_index[0]);

	if (!ops->get_rxfh_indir_size || !ops->set_rxfh ||
	    !ops->get_rxnfc)
		return -EOPNOTSUPP;

	dev_size = ops->get_rxfh_indir_size(dev);
	if (dev_size == 0)
		return -EOPNOTSUPP;

	if (copy_from_user(&user_size,
			   useraddr + offsetof(struct ethtool_rxfh_indir, size),
			   sizeof(user_size)))
		return -EFAULT;

	if (user_size != 0 && user_size != dev_size)
		return -EINVAL;

	indir = kcalloc(dev_size, sizeof(indir[0]), GFP_USER);
	if (!indir)
		return -ENOMEM;

	rx_rings.cmd = ETHTOOL_GRXRINGS;
	ret = ops->get_rxnfc(dev, &rx_rings, NULL);
	if (ret)
		goto out;

	if (user_size == 0) {
		for (i = 0; i < dev_size; i++)
			indir[i] = ethtool_rxfh_indir_default(i, rx_rings.data);
	} else {
		ret = ethtool_copy_validate_indir(indir,
						  useraddr + ringidx_offset,
						  &rx_rings,
						  dev_size);
		if (ret)
			goto out;
	}

	ret = ops->set_rxfh(dev, indir, NULL, ETH_RSS_HASH_NO_CHANGE);
	if (ret)
		goto out;

	/* indicate whether rxfh was set to default */
	if (user_size == 0)
		dev->priv_flags &= ~IFF_RXFH_CONFIGURED;
	else
		dev->priv_flags |= IFF_RXFH_CONFIGURED;

out:
	kfree(indir);
	return ret;
}

static noinline_for_stack int ethtool_get_rxfh(struct net_device *dev,
					       void __user *useraddr)
{
	int ret;
	const struct ethtool_ops *ops = dev->ethtool_ops;
	u32 user_indir_size, user_key_size;
	u32 dev_indir_size = 0, dev_key_size = 0;
	struct ethtool_rxfh rxfh;
	u32 total_size;
	u32 indir_bytes;
	u32 *indir = NULL;
	u8 dev_hfunc = 0;
	u8 *hkey = NULL;
	u8 *rss_config;

	if (!ops->get_rxfh)
		return -EOPNOTSUPP;

	if (ops->get_rxfh_indir_size)
		dev_indir_size = ops->get_rxfh_indir_size(dev);
	if (ops->get_rxfh_key_size)
		dev_key_size = ops->get_rxfh_key_size(dev);

	if (copy_from_user(&rxfh, useraddr, sizeof(rxfh)))
		return -EFAULT;
	user_indir_size = rxfh.indir_size;
	user_key_size = rxfh.key_size;

	/* Check that reserved fields are 0 for now */
	if (rxfh.rsvd8[0] || rxfh.rsvd8[1] || rxfh.rsvd8[2] || rxfh.rsvd32)
		return -EINVAL;
	/* Most drivers don't handle rss_context, check it's 0 as well */
	if (rxfh.rss_context && !ops->get_rxfh_context)
		return -EOPNOTSUPP;

	rxfh.indir_size = dev_indir_size;
	rxfh.key_size = dev_key_size;
	if (copy_to_user(useraddr, &rxfh, sizeof(rxfh)))
		return -EFAULT;

	if ((user_indir_size && (user_indir_size != dev_indir_size)) ||
	    (user_key_size && (user_key_size != dev_key_size)))
		return -EINVAL;

	indir_bytes = user_indir_size * sizeof(indir[0]);
	total_size = indir_bytes + user_key_size;
	rss_config = kzalloc(total_size, GFP_USER);
	if (!rss_config)
		return -ENOMEM;

	if (user_indir_size)
		indir = (u32 *)rss_config;

	if (user_key_size)
		hkey = rss_config + indir_bytes;

	if (rxfh.rss_context)
		ret = dev->ethtool_ops->get_rxfh_context(dev, indir, hkey,
							 &dev_hfunc,
							 rxfh.rss_context);
	else
		ret = dev->ethtool_ops->get_rxfh(dev, indir, hkey, &dev_hfunc);
	if (ret)
		goto out;

	if (copy_to_user(useraddr + offsetof(struct ethtool_rxfh, hfunc),
			 &dev_hfunc, sizeof(rxfh.hfunc))) {
		ret = -EFAULT;
	} else if (copy_to_user(useraddr +
			      offsetof(struct ethtool_rxfh, rss_config[0]),
			      rss_config, total_size)) {
		ret = -EFAULT;
	}
out:
	kfree(rss_config);

	return ret;
}

static noinline_for_stack int ethtool_set_rxfh(struct net_device *dev,
					       void __user *useraddr)
{
	int ret;
	const struct ethtool_ops *ops = dev->ethtool_ops;
	struct ethtool_rxnfc rx_rings;
	struct ethtool_rxfh rxfh;
	u32 dev_indir_size = 0, dev_key_size = 0, i;
	u32 *indir = NULL, indir_bytes = 0;
	u8 *hkey = NULL;
	u8 *rss_config;
	u32 rss_cfg_offset = offsetof(struct ethtool_rxfh, rss_config[0]);
	bool delete = false;

	if (!ops->get_rxnfc || !ops->set_rxfh)
		return -EOPNOTSUPP;

	if (ops->get_rxfh_indir_size)
		dev_indir_size = ops->get_rxfh_indir_size(dev);
	if (ops->get_rxfh_key_size)
		dev_key_size = ops->get_rxfh_key_size(dev);

	if (copy_from_user(&rxfh, useraddr, sizeof(rxfh)))
		return -EFAULT;

	/* Check that reserved fields are 0 for now */
	if (rxfh.rsvd8[0] || rxfh.rsvd8[1] || rxfh.rsvd8[2] || rxfh.rsvd32)
		return -EINVAL;
	/* Most drivers don't handle rss_context, check it's 0 as well */
	if (rxfh.rss_context && !ops->set_rxfh_context)
		return -EOPNOTSUPP;

	/* If either indir, hash key or function is valid, proceed further.
	 * Must request at least one change: indir size, hash key or function.
	 */
	if ((rxfh.indir_size &&
	     rxfh.indir_size != ETH_RXFH_INDIR_NO_CHANGE &&
	     rxfh.indir_size != dev_indir_size) ||
	    (rxfh.key_size && (rxfh.key_size != dev_key_size)) ||
	    (rxfh.indir_size == ETH_RXFH_INDIR_NO_CHANGE &&
	     rxfh.key_size == 0 && rxfh.hfunc == ETH_RSS_HASH_NO_CHANGE))
		return -EINVAL;

	if (rxfh.indir_size != ETH_RXFH_INDIR_NO_CHANGE)
		indir_bytes = dev_indir_size * sizeof(indir[0]);

	rss_config = kzalloc(indir_bytes + rxfh.key_size, GFP_USER);
	if (!rss_config)
		return -ENOMEM;

	rx_rings.cmd = ETHTOOL_GRXRINGS;
	ret = ops->get_rxnfc(dev, &rx_rings, NULL);
	if (ret)
		goto out;

	/* rxfh.indir_size == 0 means reset the indir table to default (master
	 * context) or delete the context (other RSS contexts).
	 * rxfh.indir_size == ETH_RXFH_INDIR_NO_CHANGE means leave it unchanged.
	 */
	if (rxfh.indir_size &&
	    rxfh.indir_size != ETH_RXFH_INDIR_NO_CHANGE) {
		indir = (u32 *)rss_config;
		ret = ethtool_copy_validate_indir(indir,
						  useraddr + rss_cfg_offset,
						  &rx_rings,
						  rxfh.indir_size);
		if (ret)
			goto out;
	} else if (rxfh.indir_size == 0) {
		if (rxfh.rss_context == 0) {
			indir = (u32 *)rss_config;
			for (i = 0; i < dev_indir_size; i++)
				indir[i] = ethtool_rxfh_indir_default(i, rx_rings.data);
		} else {
			delete = true;
		}
	}

	if (rxfh.key_size) {
		hkey = rss_config + indir_bytes;
		if (copy_from_user(hkey,
				   useraddr + rss_cfg_offset + indir_bytes,
				   rxfh.key_size)) {
			ret = -EFAULT;
			goto out;
		}
	}

	if (rxfh.rss_context)
		ret = ops->set_rxfh_context(dev, indir, hkey, rxfh.hfunc,
					    &rxfh.rss_context, delete);
	else
		ret = ops->set_rxfh(dev, indir, hkey, rxfh.hfunc);
	if (ret)
		goto out;

	if (copy_to_user(useraddr + offsetof(struct ethtool_rxfh, rss_context),
			 &rxfh.rss_context, sizeof(rxfh.rss_context)))
		ret = -EFAULT;

	if (!rxfh.rss_context) {
		/* indicate whether rxfh was set to default */
		if (rxfh.indir_size == 0)
			dev->priv_flags &= ~IFF_RXFH_CONFIGURED;
		else if (rxfh.indir_size != ETH_RXFH_INDIR_NO_CHANGE)
			dev->priv_flags |= IFF_RXFH_CONFIGURED;
	}

out:
	kfree(rss_config);
	return ret;
}

static int ethtool_get_regs(struct net_device *dev, char __user *useraddr)
{
	struct ethtool_regs regs;
	const struct ethtool_ops *ops = dev->ethtool_ops;
	void *regbuf;
	int reglen, ret;

	if (!ops->get_regs || !ops->get_regs_len)
		return -EOPNOTSUPP;

	if (copy_from_user(&regs, useraddr, sizeof(regs)))
		return -EFAULT;

	reglen = ops->get_regs_len(dev);
	if (reglen <= 0)
		return reglen;

	if (regs.len > reglen)
		regs.len = reglen;

	regbuf = vzalloc(reglen);
	if (!regbuf)
		return -ENOMEM;

	if (regs.len < reglen)
		reglen = regs.len;

	ops->get_regs(dev, &regs, regbuf);

	ret = -EFAULT;
	if (copy_to_user(useraddr, &regs, sizeof(regs)))
		goto out;
	useraddr += offsetof(struct ethtool_regs, data);
	if (copy_to_user(useraddr, regbuf, reglen))
		goto out;
	ret = 0;

 out:
	vfree(regbuf);
	return ret;
}

static int ethtool_reset(struct net_device *dev, char __user *useraddr)
{
	struct ethtool_value reset;
	int ret;

	if (!dev->ethtool_ops->reset)
		return -EOPNOTSUPP;

	if (copy_from_user(&reset, useraddr, sizeof(reset)))
		return -EFAULT;

	ret = dev->ethtool_ops->reset(dev, &reset.data);
	if (ret)
		return ret;

	if (copy_to_user(useraddr, &reset, sizeof(reset)))
		return -EFAULT;
	return 0;
}

static int ethtool_get_wol(struct net_device *dev, char __user *useraddr)
{
	struct ethtool_wolinfo wol;

	if (!dev->ethtool_ops->get_wol)
		return -EOPNOTSUPP;

	memset(&wol, 0, sizeof(struct ethtool_wolinfo));
	wol.cmd = ETHTOOL_GWOL;
	dev->ethtool_ops->get_wol(dev, &wol);

	if (copy_to_user(useraddr, &wol, sizeof(wol)))
		return -EFAULT;
	return 0;
}

static int ethtool_set_wol(struct net_device *dev, char __user *useraddr)
{
	struct ethtool_wolinfo wol;
	int ret;

	if (!dev->ethtool_ops->set_wol)
		return -EOPNOTSUPP;

	if (copy_from_user(&wol, useraddr, sizeof(wol)))
		return -EFAULT;

	ret = dev->ethtool_ops->set_wol(dev, &wol);
	if (ret)
		return ret;

	dev->wol_enabled = !!wol.wolopts;
	ethtool_notify(dev, ETHTOOL_MSG_WOL_NTF, NULL);

	return 0;
}

static int ethtool_get_eee(struct net_device *dev, char __user *useraddr)
{
	struct ethtool_eee edata;
	int rc;

	if (!dev->ethtool_ops->get_eee)
		return -EOPNOTSUPP;

	memset(&edata, 0, sizeof(struct ethtool_eee));
	edata.cmd = ETHTOOL_GEEE;
	rc = dev->ethtool_ops->get_eee(dev, &edata);

	if (rc)
		return rc;

	if (copy_to_user(useraddr, &edata, sizeof(edata)))
		return -EFAULT;

	return 0;
}

static int ethtool_set_eee(struct net_device *dev, char __user *useraddr)
{
	struct ethtool_eee edata;
	int ret;

	if (!dev->ethtool_ops->set_eee)
		return -EOPNOTSUPP;

	if (copy_from_user(&edata, useraddr, sizeof(edata)))
		return -EFAULT;

	ret = dev->ethtool_ops->set_eee(dev, &edata);
	if (!ret)
		ethtool_notify(dev, ETHTOOL_MSG_EEE_NTF, NULL);
	return ret;
}

static int ethtool_nway_reset(struct net_device *dev)
{
	if (!dev->ethtool_ops->nway_reset)
		return -EOPNOTSUPP;

	return dev->ethtool_ops->nway_reset(dev);
}

static int ethtool_get_link(struct net_device *dev, char __user *useraddr)
{
	struct ethtool_value edata = { .cmd = ETHTOOL_GLINK };
	int link = __ethtool_get_link(dev);

	if (link < 0)
		return link;

	edata.data = link;
	if (copy_to_user(useraddr, &edata, sizeof(edata)))
		return -EFAULT;
	return 0;
}

static int ethtool_get_any_eeprom(struct net_device *dev, void __user *useraddr,
				  int (*getter)(struct net_device *,
						struct ethtool_eeprom *, u8 *),
				  u32 total_len)
{
	struct ethtool_eeprom eeprom;
	void __user *userbuf = useraddr + sizeof(eeprom);
	u32 bytes_remaining;
	u8 *data;
	int ret = 0;

	if (copy_from_user(&eeprom, useraddr, sizeof(eeprom)))
		return -EFAULT;

	/* Check for wrap and zero */
	if (eeprom.offset + eeprom.len <= eeprom.offset)
		return -EINVAL;

	/* Check for exceeding total eeprom len */
	if (eeprom.offset + eeprom.len > total_len)
		return -EINVAL;

	data = kzalloc(PAGE_SIZE, GFP_USER);
	if (!data)
		return -ENOMEM;

	bytes_remaining = eeprom.len;
	while (bytes_remaining > 0) {
		eeprom.len = min(bytes_remaining, (u32)PAGE_SIZE);

		ret = getter(dev, &eeprom, data);
		if (ret)
			break;
		if (!eeprom.len) {
			ret = -EIO;
			break;
		}
		if (copy_to_user(userbuf, data, eeprom.len)) {
			ret = -EFAULT;
			break;
		}
		userbuf += eeprom.len;
		eeprom.offset += eeprom.len;
		bytes_remaining -= eeprom.len;
	}

	eeprom.len = userbuf - (useraddr + sizeof(eeprom));
	eeprom.offset -= eeprom.len;
	if (copy_to_user(useraddr, &eeprom, sizeof(eeprom)))
		ret = -EFAULT;

	kfree(data);
	return ret;
}

static int ethtool_get_eeprom(struct net_device *dev, void __user *useraddr)
{
	const struct ethtool_ops *ops = dev->ethtool_ops;

	if (!ops->get_eeprom || !ops->get_eeprom_len ||
	    !ops->get_eeprom_len(dev))
		return -EOPNOTSUPP;

	return ethtool_get_any_eeprom(dev, useraddr, ops->get_eeprom,
				      ops->get_eeprom_len(dev));
}

static int ethtool_set_eeprom(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_eeprom eeprom;
	const struct ethtool_ops *ops = dev->ethtool_ops;
	void __user *userbuf = useraddr + sizeof(eeprom);
	u32 bytes_remaining;
	u8 *data;
	int ret = 0;

	if (!ops->set_eeprom || !ops->get_eeprom_len ||
	    !ops->get_eeprom_len(dev))
		return -EOPNOTSUPP;

	if (copy_from_user(&eeprom, useraddr, sizeof(eeprom)))
		return -EFAULT;

	/* Check for wrap and zero */
	if (eeprom.offset + eeprom.len <= eeprom.offset)
		return -EINVAL;

	/* Check for exceeding total eeprom len */
	if (eeprom.offset + eeprom.len > ops->get_eeprom_len(dev))
		return -EINVAL;

	data = kzalloc(PAGE_SIZE, GFP_USER);
	if (!data)
		return -ENOMEM;

	bytes_remaining = eeprom.len;
	while (bytes_remaining > 0) {
		eeprom.len = min(bytes_remaining, (u32)PAGE_SIZE);

		if (copy_from_user(data, userbuf, eeprom.len)) {
			ret = -EFAULT;
			break;
		}
		ret = ops->set_eeprom(dev, &eeprom, data);
		if (ret)
			break;
		userbuf += eeprom.len;
		eeprom.offset += eeprom.len;
		bytes_remaining -= eeprom.len;
	}

	kfree(data);
	return ret;
}

static noinline_for_stack int ethtool_get_coalesce(struct net_device *dev,
						   void __user *useraddr)
{
	struct ethtool_coalesce coalesce = { .cmd = ETHTOOL_GCOALESCE };
	struct kernel_ethtool_coalesce kernel_coalesce = {};
	int ret;

	if (!dev->ethtool_ops->get_coalesce)
		return -EOPNOTSUPP;

	ret = dev->ethtool_ops->get_coalesce(dev, &coalesce, &kernel_coalesce,
					     NULL);
	if (ret)
		return ret;

	if (copy_to_user(useraddr, &coalesce, sizeof(coalesce)))
		return -EFAULT;
	return 0;
}

static bool
ethtool_set_coalesce_supported(struct net_device *dev,
			       struct ethtool_coalesce *coalesce)
{
	u32 supported_params = dev->ethtool_ops->supported_coalesce_params;
	u32 nonzero_params = 0;

	if (coalesce->rx_coalesce_usecs)
		nonzero_params |= ETHTOOL_COALESCE_RX_USECS;
	if (coalesce->rx_max_coalesced_frames)
		nonzero_params |= ETHTOOL_COALESCE_RX_MAX_FRAMES;
	if (coalesce->rx_coalesce_usecs_irq)
		nonzero_params |= ETHTOOL_COALESCE_RX_USECS_IRQ;
	if (coalesce->rx_max_coalesced_frames_irq)
		nonzero_params |= ETHTOOL_COALESCE_RX_MAX_FRAMES_IRQ;
	if (coalesce->tx_coalesce_usecs)
		nonzero_params |= ETHTOOL_COALESCE_TX_USECS;
	if (coalesce->tx_max_coalesced_frames)
		nonzero_params |= ETHTOOL_COALESCE_TX_MAX_FRAMES;
	if (coalesce->tx_coalesce_usecs_irq)
		nonzero_params |= ETHTOOL_COALESCE_TX_USECS_IRQ;
	if (coalesce->tx_max_coalesced_frames_irq)
		nonzero_params |= ETHTOOL_COALESCE_TX_MAX_FRAMES_IRQ;
	if (coalesce->stats_block_coalesce_usecs)
		nonzero_params |= ETHTOOL_COALESCE_STATS_BLOCK_USECS;
	if (coalesce->use_adaptive_rx_coalesce)
		nonzero_params |= ETHTOOL_COALESCE_USE_ADAPTIVE_RX;
	if (coalesce->use_adaptive_tx_coalesce)
		nonzero_params |= ETHTOOL_COALESCE_USE_ADAPTIVE_TX;
	if (coalesce->pkt_rate_low)
		nonzero_params |= ETHTOOL_COALESCE_PKT_RATE_LOW;
	if (coalesce->rx_coalesce_usecs_low)
		nonzero_params |= ETHTOOL_COALESCE_RX_USECS_LOW;
	if (coalesce->rx_max_coalesced_frames_low)
		nonzero_params |= ETHTOOL_COALESCE_RX_MAX_FRAMES_LOW;
	if (coalesce->tx_coalesce_usecs_low)
		nonzero_params |= ETHTOOL_COALESCE_TX_USECS_LOW;
	if (coalesce->tx_max_coalesced_frames_low)
		nonzero_params |= ETHTOOL_COALESCE_TX_MAX_FRAMES_LOW;
	if (coalesce->pkt_rate_high)
		nonzero_params |= ETHTOOL_COALESCE_PKT_RATE_HIGH;
	if (coalesce->rx_coalesce_usecs_high)
		nonzero_params |= ETHTOOL_COALESCE_RX_USECS_HIGH;
	if (coalesce->rx_max_coalesced_frames_high)
		nonzero_params |= ETHTOOL_COALESCE_RX_MAX_FRAMES_HIGH;
	if (coalesce->tx_coalesce_usecs_high)
		nonzero_params |= ETHTOOL_COALESCE_TX_USECS_HIGH;
	if (coalesce->tx_max_coalesced_frames_high)
		nonzero_params |= ETHTOOL_COALESCE_TX_MAX_FRAMES_HIGH;
	if (coalesce->rate_sample_interval)
		nonzero_params |= ETHTOOL_COALESCE_RATE_SAMPLE_INTERVAL;

	return (supported_params & nonzero_params) == nonzero_params;
}

static noinline_for_stack int ethtool_set_coalesce(struct net_device *dev,
						   void __user *useraddr)
{
	struct kernel_ethtool_coalesce kernel_coalesce = {};
	struct ethtool_coalesce coalesce;
	int ret;

	if (!dev->ethtool_ops->set_coalesce || !dev->ethtool_ops->get_coalesce)
		return -EOPNOTSUPP;

	ret = dev->ethtool_ops->get_coalesce(dev, &coalesce, &kernel_coalesce,
					     NULL);
	if (ret)
		return ret;

	if (copy_from_user(&coalesce, useraddr, sizeof(coalesce)))
		return -EFAULT;

	if (!ethtool_set_coalesce_supported(dev, &coalesce))
		return -EOPNOTSUPP;

	ret = dev->ethtool_ops->set_coalesce(dev, &coalesce, &kernel_coalesce,
					     NULL);
	if (!ret)
		ethtool_notify(dev, ETHTOOL_MSG_COALESCE_NTF, NULL);
	return ret;
}

static int ethtool_get_ringparam(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_ringparam ringparam = { .cmd = ETHTOOL_GRINGPARAM };
	struct kernel_ethtool_ringparam kernel_ringparam = {};

	if (!dev->ethtool_ops->get_ringparam)
		return -EOPNOTSUPP;

	dev->ethtool_ops->get_ringparam(dev, &ringparam,
					&kernel_ringparam, NULL);

	if (copy_to_user(useraddr, &ringparam, sizeof(ringparam)))
		return -EFAULT;
	return 0;
}

static int ethtool_set_ringparam(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_ringparam ringparam, max = { .cmd = ETHTOOL_GRINGPARAM };
	struct kernel_ethtool_ringparam kernel_ringparam;
	int ret;

	if (!dev->ethtool_ops->set_ringparam || !dev->ethtool_ops->get_ringparam)
		return -EOPNOTSUPP;

	if (copy_from_user(&ringparam, useraddr, sizeof(ringparam)))
		return -EFAULT;

	dev->ethtool_ops->get_ringparam(dev, &max, &kernel_ringparam, NULL);

	/* ensure new ring parameters are within the maximums */
	if (ringparam.rx_pending > max.rx_max_pending ||
	    ringparam.rx_mini_pending > max.rx_mini_max_pending ||
	    ringparam.rx_jumbo_pending > max.rx_jumbo_max_pending ||
	    ringparam.tx_pending > max.tx_max_pending)
		return -EINVAL;

	ret = dev->ethtool_ops->set_ringparam(dev, &ringparam,
					      &kernel_ringparam, NULL);
	if (!ret)
		ethtool_notify(dev, ETHTOOL_MSG_RINGS_NTF, NULL);
	return ret;
}

static noinline_for_stack int ethtool_get_channels(struct net_device *dev,
						   void __user *useraddr)
{
	struct ethtool_channels channels = { .cmd = ETHTOOL_GCHANNELS };

	if (!dev->ethtool_ops->get_channels)
		return -EOPNOTSUPP;

	dev->ethtool_ops->get_channels(dev, &channels);

	if (copy_to_user(useraddr, &channels, sizeof(channels)))
		return -EFAULT;
	return 0;
}

static noinline_for_stack int ethtool_set_channels(struct net_device *dev,
						   void __user *useraddr)
{
	struct ethtool_channels channels, curr = { .cmd = ETHTOOL_GCHANNELS };
	u16 from_channel, to_channel;
	u32 max_rx_in_use = 0;
	unsigned int i;
	int ret;

	if (!dev->ethtool_ops->set_channels || !dev->ethtool_ops->get_channels)
		return -EOPNOTSUPP;

	if (copy_from_user(&channels, useraddr, sizeof(channels)))
		return -EFAULT;

	dev->ethtool_ops->get_channels(dev, &curr);

	if (channels.rx_count == curr.rx_count &&
	    channels.tx_count == curr.tx_count &&
	    channels.combined_count == curr.combined_count &&
	    channels.other_count == curr.other_count)
		return 0;

	/* ensure new counts are within the maximums */
	if (channels.rx_count > curr.max_rx ||
	    channels.tx_count > curr.max_tx ||
	    channels.combined_count > curr.max_combined ||
	    channels.other_count > curr.max_other)
		return -EINVAL;

	/* ensure there is at least one RX and one TX channel */
	if (!channels.combined_count &&
	    (!channels.rx_count || !channels.tx_count))
		return -EINVAL;

	/* ensure the new Rx count fits within the configured Rx flow
	 * indirection table settings */
	if (netif_is_rxfh_configured(dev) &&
	    !ethtool_get_max_rxfh_channel(dev, &max_rx_in_use) &&
	    (channels.combined_count + channels.rx_count) <= max_rx_in_use)
	    return -EINVAL;

	/* Disabling channels, query zero-copy AF_XDP sockets */
	from_channel = channels.combined_count +
		min(channels.rx_count, channels.tx_count);
	to_channel = curr.combined_count + max(curr.rx_count, curr.tx_count);
	for (i = from_channel; i < to_channel; i++)
		if (xsk_get_pool_from_qid(dev, i))
			return -EINVAL;

	ret = dev->ethtool_ops->set_channels(dev, &channels);
	if (!ret)
		ethtool_notify(dev, ETHTOOL_MSG_CHANNELS_NTF, NULL);
	return ret;
}

static int ethtool_get_pauseparam(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_pauseparam pauseparam = { .cmd = ETHTOOL_GPAUSEPARAM };

	if (!dev->ethtool_ops->get_pauseparam)
		return -EOPNOTSUPP;

	dev->ethtool_ops->get_pauseparam(dev, &pauseparam);

	if (copy_to_user(useraddr, &pauseparam, sizeof(pauseparam)))
		return -EFAULT;
	return 0;
}

static int ethtool_set_pauseparam(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_pauseparam pauseparam;
	int ret;

	if (!dev->ethtool_ops->set_pauseparam)
		return -EOPNOTSUPP;

	if (copy_from_user(&pauseparam, useraddr, sizeof(pauseparam)))
		return -EFAULT;

	ret = dev->ethtool_ops->set_pauseparam(dev, &pauseparam);
	if (!ret)
		ethtool_notify(dev, ETHTOOL_MSG_PAUSE_NTF, NULL);
	return ret;
}

static int ethtool_self_test(struct net_device *dev, char __user *useraddr)
{
	struct ethtool_test test;
	const struct ethtool_ops *ops = dev->ethtool_ops;
	u64 *data;
	int ret, test_len;

	if (!ops->self_test || !ops->get_sset_count)
		return -EOPNOTSUPP;

	test_len = ops->get_sset_count(dev, ETH_SS_TEST);
	if (test_len < 0)
		return test_len;
	WARN_ON(test_len == 0);

	if (copy_from_user(&test, useraddr, sizeof(test)))
		return -EFAULT;

	test.len = test_len;
	data = kcalloc(test_len, sizeof(u64), GFP_USER);
	if (!data)
		return -ENOMEM;

	netif_testing_on(dev);
	ops->self_test(dev, &test, data);
	netif_testing_off(dev);

	ret = -EFAULT;
	if (copy_to_user(useraddr, &test, sizeof(test)))
		goto out;
	useraddr += sizeof(test);
	if (copy_to_user(useraddr, data, array_size(test.len, sizeof(u64))))
		goto out;
	ret = 0;

 out:
	kfree(data);
	return ret;
}

static int ethtool_get_strings(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_gstrings gstrings;
	u8 *data;
	int ret;

	if (copy_from_user(&gstrings, useraddr, sizeof(gstrings)))
		return -EFAULT;

	ret = __ethtool_get_sset_count(dev, gstrings.string_set);
	if (ret < 0)
		return ret;
	if (ret > S32_MAX / ETH_GSTRING_LEN)
		return -ENOMEM;
	WARN_ON_ONCE(!ret);

	gstrings.len = ret;

	if (gstrings.len) {
		data = vzalloc(array_size(gstrings.len, ETH_GSTRING_LEN));
		if (!data)
			return -ENOMEM;

		__ethtool_get_strings(dev, gstrings.string_set, data);
	} else {
		data = NULL;
	}

	ret = -EFAULT;
	if (copy_to_user(useraddr, &gstrings, sizeof(gstrings)))
		goto out;
	useraddr += sizeof(gstrings);
	if (gstrings.len &&
	    copy_to_user(useraddr, data,
			 array_size(gstrings.len, ETH_GSTRING_LEN)))
		goto out;
	ret = 0;

out:
	vfree(data);
	return ret;
}

__printf(2, 3) void ethtool_sprintf(u8 **data, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsnprintf(*data, ETH_GSTRING_LEN, fmt, args);
	va_end(args);

	*data += ETH_GSTRING_LEN;
}
EXPORT_SYMBOL(ethtool_sprintf);

static int ethtool_phys_id(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_value id;
	static bool busy;
	const struct ethtool_ops *ops = dev->ethtool_ops;
	netdevice_tracker dev_tracker;
	int rc;

	if (!ops->set_phys_id)
		return -EOPNOTSUPP;

	if (busy)
		return -EBUSY;

	if (copy_from_user(&id, useraddr, sizeof(id)))
		return -EFAULT;

	rc = ops->set_phys_id(dev, ETHTOOL_ID_ACTIVE);
	if (rc < 0)
		return rc;

	/* Drop the RTNL lock while waiting, but prevent reentry or
	 * removal of the device.
	 */
	busy = true;
	netdev_hold(dev, &dev_tracker, GFP_KERNEL);
	rtnl_unlock();

	if (rc == 0) {
		/* Driver will handle this itself */
		schedule_timeout_interruptible(
			id.data ? (id.data * HZ) : MAX_SCHEDULE_TIMEOUT);
	} else {
		/* Driver expects to be called at twice the frequency in rc */
		int n = rc * 2, interval = HZ / n;
		u64 count = mul_u32_u32(n, id.data);
		u64 i = 0;

		do {
			rtnl_lock();
			rc = ops->set_phys_id(dev,
				    (i++ & 1) ? ETHTOOL_ID_OFF : ETHTOOL_ID_ON);
			rtnl_unlock();
			if (rc)
				break;
			schedule_timeout_interruptible(interval);
		} while (!signal_pending(current) && (!id.data || i < count));
	}

	rtnl_lock();
	netdev_put(dev, &dev_tracker);
	busy = false;

	(void) ops->set_phys_id(dev, ETHTOOL_ID_INACTIVE);
	return rc;
}

static int ethtool_get_stats(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_stats stats;
	const struct ethtool_ops *ops = dev->ethtool_ops;
	u64 *data;
	int ret, n_stats;

	if (!ops->get_ethtool_stats || !ops->get_sset_count)
		return -EOPNOTSUPP;

	n_stats = ops->get_sset_count(dev, ETH_SS_STATS);
	if (n_stats < 0)
		return n_stats;
	if (n_stats > S32_MAX / sizeof(u64))
		return -ENOMEM;
	WARN_ON_ONCE(!n_stats);
	if (copy_from_user(&stats, useraddr, sizeof(stats)))
		return -EFAULT;

	stats.n_stats = n_stats;

	if (n_stats) {
		data = vzalloc(array_size(n_stats, sizeof(u64)));
		if (!data)
			return -ENOMEM;
		ops->get_ethtool_stats(dev, &stats, data);
	} else {
		data = NULL;
	}

	ret = -EFAULT;
	if (copy_to_user(useraddr, &stats, sizeof(stats)))
		goto out;
	useraddr += sizeof(stats);
	if (n_stats && copy_to_user(useraddr, data, array_size(n_stats, sizeof(u64))))
		goto out;
	ret = 0;

 out:
	vfree(data);
	return ret;
}

static int ethtool_get_phy_stats(struct net_device *dev, void __user *useraddr)
{
	const struct ethtool_phy_ops *phy_ops = ethtool_phy_ops;
	const struct ethtool_ops *ops = dev->ethtool_ops;
	struct phy_device *phydev = dev->phydev;
	struct ethtool_stats stats;
	u64 *data;
	int ret, n_stats;

	if (!phydev && (!ops->get_ethtool_phy_stats || !ops->get_sset_count))
		return -EOPNOTSUPP;

	if (phydev && !ops->get_ethtool_phy_stats &&
	    phy_ops && phy_ops->get_sset_count)
		n_stats = phy_ops->get_sset_count(phydev);
	else
		n_stats = ops->get_sset_count(dev, ETH_SS_PHY_STATS);
	if (n_stats < 0)
		return n_stats;
	if (n_stats > S32_MAX / sizeof(u64))
		return -ENOMEM;
	if (WARN_ON_ONCE(!n_stats))
		return -EOPNOTSUPP;

	if (copy_from_user(&stats, useraddr, sizeof(stats)))
		return -EFAULT;

	stats.n_stats = n_stats;

	if (n_stats) {
		data = vzalloc(array_size(n_stats, sizeof(u64)));
		if (!data)
			return -ENOMEM;

		if (phydev && !ops->get_ethtool_phy_stats &&
		    phy_ops && phy_ops->get_stats) {
			ret = phy_ops->get_stats(phydev, &stats, data);
			if (ret < 0)
				goto out;
		} else {
			ops->get_ethtool_phy_stats(dev, &stats, data);
		}
	} else {
		data = NULL;
	}

	ret = -EFAULT;
	if (copy_to_user(useraddr, &stats, sizeof(stats)))
		goto out;
	useraddr += sizeof(stats);
	if (n_stats && copy_to_user(useraddr, data, array_size(n_stats, sizeof(u64))))
		goto out;
	ret = 0;

 out:
	vfree(data);
	return ret;
}

static int ethtool_get_perm_addr(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_perm_addr epaddr;

	if (copy_from_user(&epaddr, useraddr, sizeof(epaddr)))
		return -EFAULT;

	if (epaddr.size < dev->addr_len)
		return -ETOOSMALL;
	epaddr.size = dev->addr_len;

	if (copy_to_user(useraddr, &epaddr, sizeof(epaddr)))
		return -EFAULT;
	useraddr += sizeof(epaddr);
	if (copy_to_user(useraddr, dev->perm_addr, epaddr.size))
		return -EFAULT;
	return 0;
}

static int ethtool_get_value(struct net_device *dev, char __user *useraddr,
			     u32 cmd, u32 (*actor)(struct net_device *))
{
	struct ethtool_value edata = { .cmd = cmd };

	if (!actor)
		return -EOPNOTSUPP;

	edata.data = actor(dev);

	if (copy_to_user(useraddr, &edata, sizeof(edata)))
		return -EFAULT;
	return 0;
}

static int ethtool_set_value_void(struct net_device *dev, char __user *useraddr,
			     void (*actor)(struct net_device *, u32))
{
	struct ethtool_value edata;

	if (!actor)
		return -EOPNOTSUPP;

	if (copy_from_user(&edata, useraddr, sizeof(edata)))
		return -EFAULT;

	actor(dev, edata.data);
	return 0;
}

static int ethtool_set_value(struct net_device *dev, char __user *useraddr,
			     int (*actor)(struct net_device *, u32))
{
	struct ethtool_value edata;

	if (!actor)
		return -EOPNOTSUPP;

	if (copy_from_user(&edata, useraddr, sizeof(edata)))
		return -EFAULT;

	return actor(dev, edata.data);
}

static int
ethtool_flash_device(struct net_device *dev, struct ethtool_devlink_compat *req)
{
	if (!dev->ethtool_ops->flash_device) {
		req->devlink = netdev_to_devlink_get(dev);
		return 0;
	}

	return dev->ethtool_ops->flash_device(dev, &req->efl);
}

static int ethtool_set_dump(struct net_device *dev,
			void __user *useraddr)
{
	struct ethtool_dump dump;

	if (!dev->ethtool_ops->set_dump)
		return -EOPNOTSUPP;

	if (copy_from_user(&dump, useraddr, sizeof(dump)))
		return -EFAULT;

	return dev->ethtool_ops->set_dump(dev, &dump);
}

static int ethtool_get_dump_flag(struct net_device *dev,
				void __user *useraddr)
{
	int ret;
	struct ethtool_dump dump;
	const struct ethtool_ops *ops = dev->ethtool_ops;

	if (!ops->get_dump_flag)
		return -EOPNOTSUPP;

	if (copy_from_user(&dump, useraddr, sizeof(dump)))
		return -EFAULT;

	ret = ops->get_dump_flag(dev, &dump);
	if (ret)
		return ret;

	if (copy_to_user(useraddr, &dump, sizeof(dump)))
		return -EFAULT;
	return 0;
}

static int ethtool_get_dump_data(struct net_device *dev,
				void __user *useraddr)
{
	int ret;
	__u32 len;
	struct ethtool_dump dump, tmp;
	const struct ethtool_ops *ops = dev->ethtool_ops;
	void *data = NULL;

	if (!ops->get_dump_data || !ops->get_dump_flag)
		return -EOPNOTSUPP;

	if (copy_from_user(&dump, useraddr, sizeof(dump)))
		return -EFAULT;

	memset(&tmp, 0, sizeof(tmp));
	tmp.cmd = ETHTOOL_GET_DUMP_FLAG;
	ret = ops->get_dump_flag(dev, &tmp);
	if (ret)
		return ret;

	len = min(tmp.len, dump.len);
	if (!len)
		return -EFAULT;

	/* Don't ever let the driver think there's more space available
	 * than it requested with .get_dump_flag().
	 */
	dump.len = len;

	/* Always allocate enough space to hold the whole thing so that the
	 * driver does not need to check the length and bother with partial
	 * dumping.
	 */
	data = vzalloc(tmp.len);
	if (!data)
		return -ENOMEM;
	ret = ops->get_dump_data(dev, &dump, data);
	if (ret)
		goto out;

	/* There are two sane possibilities:
	 * 1. The driver's .get_dump_data() does not touch dump.len.
	 * 2. Or it may set dump.len to how much it really writes, which
	 *    should be tmp.len (or len if it can do a partial dump).
	 * In any case respond to userspace with the actual length of data
	 * it's receiving.
	 */
	WARN_ON(dump.len != len && dump.len != tmp.len);
	dump.len = len;

	if (copy_to_user(useraddr, &dump, sizeof(dump))) {
		ret = -EFAULT;
		goto out;
	}
	useraddr += offsetof(struct ethtool_dump, data);
	if (copy_to_user(useraddr, data, len))
		ret = -EFAULT;
out:
	vfree(data);
	return ret;
}

static int ethtool_get_ts_info(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_ts_info info;
	int err;

	err = __ethtool_get_ts_info(dev, &info);
	if (err)
		return err;

	if (copy_to_user(useraddr, &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

int ethtool_get_module_info_call(struct net_device *dev,
				 struct ethtool_modinfo *modinfo)
{
	const struct ethtool_ops *ops = dev->ethtool_ops;
	struct phy_device *phydev = dev->phydev;

	if (dev->sfp_bus)
		return sfp_get_module_info(dev->sfp_bus, modinfo);

	if (phydev && phydev->drv && phydev->drv->module_info)
		return phydev->drv->module_info(phydev, modinfo);

	if (ops->get_module_info)
		return ops->get_module_info(dev, modinfo);

	return -EOPNOTSUPP;
}

static int ethtool_get_module_info(struct net_device *dev,
				   void __user *useraddr)
{
	int ret;
	struct ethtool_modinfo modinfo;

	if (copy_from_user(&modinfo, useraddr, sizeof(modinfo)))
		return -EFAULT;

	ret = ethtool_get_module_info_call(dev, &modinfo);
	if (ret)
		return ret;

	if (copy_to_user(useraddr, &modinfo, sizeof(modinfo)))
		return -EFAULT;

	return 0;
}

int ethtool_get_module_eeprom_call(struct net_device *dev,
				   struct ethtool_eeprom *ee, u8 *data)
{
	const struct ethtool_ops *ops = dev->ethtool_ops;
	struct phy_device *phydev = dev->phydev;

	if (dev->sfp_bus)
		return sfp_get_module_eeprom(dev->sfp_bus, ee, data);

	if (phydev && phydev->drv && phydev->drv->module_eeprom)
		return phydev->drv->module_eeprom(phydev, ee, data);

	if (ops->get_module_eeprom)
		return ops->get_module_eeprom(dev, ee, data);

	return -EOPNOTSUPP;
}

static int ethtool_get_module_eeprom(struct net_device *dev,
				     void __user *useraddr)
{
	int ret;
	struct ethtool_modinfo modinfo;

	ret = ethtool_get_module_info_call(dev, &modinfo);
	if (ret)
		return ret;

	return ethtool_get_any_eeprom(dev, useraddr,
				      ethtool_get_module_eeprom_call,
				      modinfo.eeprom_len);
}

static int ethtool_tunable_valid(const struct ethtool_tunable *tuna)
{
	switch (tuna->id) {
	case ETHTOOL_RX_COPYBREAK:
	case ETHTOOL_TX_COPYBREAK:
	case ETHTOOL_TX_COPYBREAK_BUF_SIZE:
		if (tuna->len != sizeof(u32) ||
		    tuna->type_id != ETHTOOL_TUNABLE_U32)
			return -EINVAL;
		break;
	case ETHTOOL_PFC_PREVENTION_TOUT:
		if (tuna->len != sizeof(u16) ||
		    tuna->type_id != ETHTOOL_TUNABLE_U16)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int ethtool_get_tunable(struct net_device *dev, void __user *useraddr)
{
	int ret;
	struct ethtool_tunable tuna;
	const struct ethtool_ops *ops = dev->ethtool_ops;
	void *data;

	if (!ops->get_tunable)
		return -EOPNOTSUPP;
	if (copy_from_user(&tuna, useraddr, sizeof(tuna)))
		return -EFAULT;
	ret = ethtool_tunable_valid(&tuna);
	if (ret)
		return ret;
	data = kzalloc(tuna.len, GFP_USER);
	if (!data)
		return -ENOMEM;
	ret = ops->get_tunable(dev, &tuna, data);
	if (ret)
		goto out;
	useraddr += sizeof(tuna);
	ret = -EFAULT;
	if (copy_to_user(useraddr, data, tuna.len))
		goto out;
	ret = 0;

out:
	kfree(data);
	return ret;
}

static int ethtool_set_tunable(struct net_device *dev, void __user *useraddr)
{
	int ret;
	struct ethtool_tunable tuna;
	const struct ethtool_ops *ops = dev->ethtool_ops;
	void *data;

	if (!ops->set_tunable)
		return -EOPNOTSUPP;
	if (copy_from_user(&tuna, useraddr, sizeof(tuna)))
		return -EFAULT;
	ret = ethtool_tunable_valid(&tuna);
	if (ret)
		return ret;
	useraddr += sizeof(tuna);
	data = memdup_user(useraddr, tuna.len);
	if (IS_ERR(data))
		return PTR_ERR(data);
	ret = ops->set_tunable(dev, &tuna, data);

	kfree(data);
	return ret;
}

static noinline_for_stack int
ethtool_get_per_queue_coalesce(struct net_device *dev,
			       void __user *useraddr,
			       struct ethtool_per_queue_op *per_queue_opt)
{
	u32 bit;
	int ret;
	DECLARE_BITMAP(queue_mask, MAX_NUM_QUEUE);

	if (!dev->ethtool_ops->get_per_queue_coalesce)
		return -EOPNOTSUPP;

	useraddr += sizeof(*per_queue_opt);

	bitmap_from_arr32(queue_mask, per_queue_opt->queue_mask,
			  MAX_NUM_QUEUE);

	for_each_set_bit(bit, queue_mask, MAX_NUM_QUEUE) {
		struct ethtool_coalesce coalesce = { .cmd = ETHTOOL_GCOALESCE };

		ret = dev->ethtool_ops->get_per_queue_coalesce(dev, bit, &coalesce);
		if (ret != 0)
			return ret;
		if (copy_to_user(useraddr, &coalesce, sizeof(coalesce)))
			return -EFAULT;
		useraddr += sizeof(coalesce);
	}

	return 0;
}

static noinline_for_stack int
ethtool_set_per_queue_coalesce(struct net_device *dev,
			       void __user *useraddr,
			       struct ethtool_per_queue_op *per_queue_opt)
{
	u32 bit;
	int i, ret = 0;
	int n_queue;
	struct ethtool_coalesce *backup = NULL, *tmp = NULL;
	DECLARE_BITMAP(queue_mask, MAX_NUM_QUEUE);

	if ((!dev->ethtool_ops->set_per_queue_coalesce) ||
	    (!dev->ethtool_ops->get_per_queue_coalesce))
		return -EOPNOTSUPP;

	useraddr += sizeof(*per_queue_opt);

	bitmap_from_arr32(queue_mask, per_queue_opt->queue_mask, MAX_NUM_QUEUE);
	n_queue = bitmap_weight(queue_mask, MAX_NUM_QUEUE);
	tmp = backup = kmalloc_array(n_queue, sizeof(*backup), GFP_KERNEL);
	if (!backup)
		return -ENOMEM;

	for_each_set_bit(bit, queue_mask, MAX_NUM_QUEUE) {
		struct ethtool_coalesce coalesce;

		ret = dev->ethtool_ops->get_per_queue_coalesce(dev, bit, tmp);
		if (ret != 0)
			goto roll_back;

		tmp++;

		if (copy_from_user(&coalesce, useraddr, sizeof(coalesce))) {
			ret = -EFAULT;
			goto roll_back;
		}

		if (!ethtool_set_coalesce_supported(dev, &coalesce)) {
			ret = -EOPNOTSUPP;
			goto roll_back;
		}

		ret = dev->ethtool_ops->set_per_queue_coalesce(dev, bit, &coalesce);
		if (ret != 0)
			goto roll_back;

		useraddr += sizeof(coalesce);
	}

roll_back:
	if (ret != 0) {
		tmp = backup;
		for_each_set_bit(i, queue_mask, bit) {
			dev->ethtool_ops->set_per_queue_coalesce(dev, i, tmp);
			tmp++;
		}
	}
	kfree(backup);

	return ret;
}

static int noinline_for_stack ethtool_set_per_queue(struct net_device *dev,
				 void __user *useraddr, u32 sub_cmd)
{
	struct ethtool_per_queue_op per_queue_opt;

	if (copy_from_user(&per_queue_opt, useraddr, sizeof(per_queue_opt)))
		return -EFAULT;

	if (per_queue_opt.sub_command != sub_cmd)
		return -EINVAL;

	switch (per_queue_opt.sub_command) {
	case ETHTOOL_GCOALESCE:
		return ethtool_get_per_queue_coalesce(dev, useraddr, &per_queue_opt);
	case ETHTOOL_SCOALESCE:
		return ethtool_set_per_queue_coalesce(dev, useraddr, &per_queue_opt);
	default:
		return -EOPNOTSUPP;
	}
}

static int ethtool_phy_tunable_valid(const struct ethtool_tunable *tuna)
{
	switch (tuna->id) {
	case ETHTOOL_PHY_DOWNSHIFT:
	case ETHTOOL_PHY_FAST_LINK_DOWN:
		if (tuna->len != sizeof(u8) ||
		    tuna->type_id != ETHTOOL_TUNABLE_U8)
			return -EINVAL;
		break;
	case ETHTOOL_PHY_EDPD:
		if (tuna->len != sizeof(u16) ||
		    tuna->type_id != ETHTOOL_TUNABLE_U16)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int get_phy_tunable(struct net_device *dev, void __user *useraddr)
{
	struct phy_device *phydev = dev->phydev;
	struct ethtool_tunable tuna;
	bool phy_drv_tunable;
	void *data;
	int ret;

	phy_drv_tunable = phydev && phydev->drv && phydev->drv->get_tunable;
	if (!phy_drv_tunable && !dev->ethtool_ops->get_phy_tunable)
		return -EOPNOTSUPP;
	if (copy_from_user(&tuna, useraddr, sizeof(tuna)))
		return -EFAULT;
	ret = ethtool_phy_tunable_valid(&tuna);
	if (ret)
		return ret;
	data = kzalloc(tuna.len, GFP_USER);
	if (!data)
		return -ENOMEM;
	if (phy_drv_tunable) {
		mutex_lock(&phydev->lock);
		ret = phydev->drv->get_tunable(phydev, &tuna, data);
		mutex_unlock(&phydev->lock);
	} else {
		ret = dev->ethtool_ops->get_phy_tunable(dev, &tuna, data);
	}
	if (ret)
		goto out;
	useraddr += sizeof(tuna);
	ret = -EFAULT;
	if (copy_to_user(useraddr, data, tuna.len))
		goto out;
	ret = 0;

out:
	kfree(data);
	return ret;
}

static int set_phy_tunable(struct net_device *dev, void __user *useraddr)
{
	struct phy_device *phydev = dev->phydev;
	struct ethtool_tunable tuna;
	bool phy_drv_tunable;
	void *data;
	int ret;

	phy_drv_tunable = phydev && phydev->drv && phydev->drv->get_tunable;
	if (!phy_drv_tunable && !dev->ethtool_ops->set_phy_tunable)
		return -EOPNOTSUPP;
	if (copy_from_user(&tuna, useraddr, sizeof(tuna)))
		return -EFAULT;
	ret = ethtool_phy_tunable_valid(&tuna);
	if (ret)
		return ret;
	useraddr += sizeof(tuna);
	data = memdup_user(useraddr, tuna.len);
	if (IS_ERR(data))
		return PTR_ERR(data);
	if (phy_drv_tunable) {
		mutex_lock(&phydev->lock);
		ret = phydev->drv->set_tunable(phydev, &tuna, data);
		mutex_unlock(&phydev->lock);
	} else {
		ret = dev->ethtool_ops->set_phy_tunable(dev, &tuna, data);
	}

	kfree(data);
	return ret;
}

static int ethtool_get_fecparam(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_fecparam fecparam = { .cmd = ETHTOOL_GFECPARAM };
	int rc;

	if (!dev->ethtool_ops->get_fecparam)
		return -EOPNOTSUPP;

	rc = dev->ethtool_ops->get_fecparam(dev, &fecparam);
	if (rc)
		return rc;

	if (WARN_ON_ONCE(fecparam.reserved))
		fecparam.reserved = 0;

	if (copy_to_user(useraddr, &fecparam, sizeof(fecparam)))
		return -EFAULT;
	return 0;
}

static int ethtool_set_fecparam(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_fecparam fecparam;

	if (!dev->ethtool_ops->set_fecparam)
		return -EOPNOTSUPP;

	if (copy_from_user(&fecparam, useraddr, sizeof(fecparam)))
		return -EFAULT;

	if (!fecparam.fec || fecparam.fec & ETHTOOL_FEC_NONE)
		return -EINVAL;

	fecparam.active_fec = 0;
	fecparam.reserved = 0;

	return dev->ethtool_ops->set_fecparam(dev, &fecparam);
}

/* The main entry point in this file.  Called from net/core/dev_ioctl.c */

static int
__dev_ethtool(struct net *net, struct ifreq *ifr, void __user *useraddr,
	      u32 ethcmd, struct ethtool_devlink_compat *devlink_state)
{
	struct net_device *dev;
	u32 sub_cmd;
	int rc;
	netdev_features_t old_features;

	dev = __dev_get_by_name(net, ifr->ifr_name);
	if (!dev)
		return -ENODEV;

	if (ethcmd == ETHTOOL_PERQUEUE) {
		if (copy_from_user(&sub_cmd, useraddr + sizeof(ethcmd), sizeof(sub_cmd)))
			return -EFAULT;
	} else {
		sub_cmd = ethcmd;
	}
	/* Allow some commands to be done by anyone */
	switch (sub_cmd) {
	case ETHTOOL_GSET:
	case ETHTOOL_GDRVINFO:
	case ETHTOOL_GMSGLVL:
	case ETHTOOL_GLINK:
	case ETHTOOL_GCOALESCE:
	case ETHTOOL_GRINGPARAM:
	case ETHTOOL_GPAUSEPARAM:
	case ETHTOOL_GRXCSUM:
	case ETHTOOL_GTXCSUM:
	case ETHTOOL_GSG:
	case ETHTOOL_GSSET_INFO:
	case ETHTOOL_GSTRINGS:
	case ETHTOOL_GSTATS:
	case ETHTOOL_GPHYSTATS:
	case ETHTOOL_GTSO:
	case ETHTOOL_GPERMADDR:
	case ETHTOOL_GUFO:
	case ETHTOOL_GGSO:
	case ETHTOOL_GGRO:
	case ETHTOOL_GFLAGS:
	case ETHTOOL_GPFLAGS:
	case ETHTOOL_GRXFH:
	case ETHTOOL_GRXRINGS:
	case ETHTOOL_GRXCLSRLCNT:
	case ETHTOOL_GRXCLSRULE:
	case ETHTOOL_GRXCLSRLALL:
	case ETHTOOL_GRXFHINDIR:
	case ETHTOOL_GRSSH:
	case ETHTOOL_GFEATURES:
	case ETHTOOL_GCHANNELS:
	case ETHTOOL_GET_TS_INFO:
	case ETHTOOL_GEEE:
	case ETHTOOL_GTUNABLE:
	case ETHTOOL_PHY_GTUNABLE:
	case ETHTOOL_GLINKSETTINGS:
	case ETHTOOL_GFECPARAM:
		break;
	default:
		if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
			return -EPERM;
	}

	if (dev->dev.parent)
		pm_runtime_get_sync(dev->dev.parent);

	if (!netif_device_present(dev)) {
		rc = -ENODEV;
		goto out;
	}

	if (dev->ethtool_ops->begin) {
		rc = dev->ethtool_ops->begin(dev);
		if (rc < 0)
			goto out;
	}
	old_features = dev->features;

	switch (ethcmd) {
	case ETHTOOL_GSET:
		rc = ethtool_get_settings(dev, useraddr);
		break;
	case ETHTOOL_SSET:
		rc = ethtool_set_settings(dev, useraddr);
		break;
	case ETHTOOL_GDRVINFO:
		rc = ethtool_get_drvinfo(dev, devlink_state);
		break;
	case ETHTOOL_GREGS:
		rc = ethtool_get_regs(dev, useraddr);
		break;
	case ETHTOOL_GWOL:
		rc = ethtool_get_wol(dev, useraddr);
		break;
	case ETHTOOL_SWOL:
		rc = ethtool_set_wol(dev, useraddr);
		break;
	case ETHTOOL_GMSGLVL:
		rc = ethtool_get_value(dev, useraddr, ethcmd,
				       dev->ethtool_ops->get_msglevel);
		break;
	case ETHTOOL_SMSGLVL:
		rc = ethtool_set_value_void(dev, useraddr,
				       dev->ethtool_ops->set_msglevel);
		if (!rc)
			ethtool_notify(dev, ETHTOOL_MSG_DEBUG_NTF, NULL);
		break;
	case ETHTOOL_GEEE:
		rc = ethtool_get_eee(dev, useraddr);
		break;
	case ETHTOOL_SEEE:
		rc = ethtool_set_eee(dev, useraddr);
		break;
	case ETHTOOL_NWAY_RST:
		rc = ethtool_nway_reset(dev);
		break;
	case ETHTOOL_GLINK:
		rc = ethtool_get_link(dev, useraddr);
		break;
	case ETHTOOL_GEEPROM:
		rc = ethtool_get_eeprom(dev, useraddr);
		break;
	case ETHTOOL_SEEPROM:
		rc = ethtool_set_eeprom(dev, useraddr);
		break;
	case ETHTOOL_GCOALESCE:
		rc = ethtool_get_coalesce(dev, useraddr);
		break;
	case ETHTOOL_SCOALESCE:
		rc = ethtool_set_coalesce(dev, useraddr);
		break;
	case ETHTOOL_GRINGPARAM:
		rc = ethtool_get_ringparam(dev, useraddr);
		break;
	case ETHTOOL_SRINGPARAM:
		rc = ethtool_set_ringparam(dev, useraddr);
		break;
	case ETHTOOL_GPAUSEPARAM:
		rc = ethtool_get_pauseparam(dev, useraddr);
		break;
	case ETHTOOL_SPAUSEPARAM:
		rc = ethtool_set_pauseparam(dev, useraddr);
		break;
	case ETHTOOL_TEST:
		rc = ethtool_self_test(dev, useraddr);
		break;
	case ETHTOOL_GSTRINGS:
		rc = ethtool_get_strings(dev, useraddr);
		break;
	case ETHTOOL_PHYS_ID:
		rc = ethtool_phys_id(dev, useraddr);
		break;
	case ETHTOOL_GSTATS:
		rc = ethtool_get_stats(dev, useraddr);
		break;
	case ETHTOOL_GPERMADDR:
		rc = ethtool_get_perm_addr(dev, useraddr);
		break;
	case ETHTOOL_GFLAGS:
		rc = ethtool_get_value(dev, useraddr, ethcmd,
					__ethtool_get_flags);
		break;
	case ETHTOOL_SFLAGS:
		rc = ethtool_set_value(dev, useraddr, __ethtool_set_flags);
		break;
	case ETHTOOL_GPFLAGS:
		rc = ethtool_get_value(dev, useraddr, ethcmd,
				       dev->ethtool_ops->get_priv_flags);
		if (!rc)
			ethtool_notify(dev, ETHTOOL_MSG_PRIVFLAGS_NTF, NULL);
		break;
	case ETHTOOL_SPFLAGS:
		rc = ethtool_set_value(dev, useraddr,
				       dev->ethtool_ops->set_priv_flags);
		break;
	case ETHTOOL_GRXFH:
	case ETHTOOL_GRXRINGS:
	case ETHTOOL_GRXCLSRLCNT:
	case ETHTOOL_GRXCLSRULE:
	case ETHTOOL_GRXCLSRLALL:
		rc = ethtool_get_rxnfc(dev, ethcmd, useraddr);
		break;
	case ETHTOOL_SRXFH:
	case ETHTOOL_SRXCLSRLDEL:
	case ETHTOOL_SRXCLSRLINS:
		rc = ethtool_set_rxnfc(dev, ethcmd, useraddr);
		break;
	case ETHTOOL_FLASHDEV:
		rc = ethtool_flash_device(dev, devlink_state);
		break;
	case ETHTOOL_RESET:
		rc = ethtool_reset(dev, useraddr);
		break;
	case ETHTOOL_GSSET_INFO:
		rc = ethtool_get_sset_info(dev, useraddr);
		break;
	case ETHTOOL_GRXFHINDIR:
		rc = ethtool_get_rxfh_indir(dev, useraddr);
		break;
	case ETHTOOL_SRXFHINDIR:
		rc = ethtool_set_rxfh_indir(dev, useraddr);
		break;
	case ETHTOOL_GRSSH:
		rc = ethtool_get_rxfh(dev, useraddr);
		break;
	case ETHTOOL_SRSSH:
		rc = ethtool_set_rxfh(dev, useraddr);
		break;
	case ETHTOOL_GFEATURES:
		rc = ethtool_get_features(dev, useraddr);
		break;
	case ETHTOOL_SFEATURES:
		rc = ethtool_set_features(dev, useraddr);
		break;
	case ETHTOOL_GTXCSUM:
	case ETHTOOL_GRXCSUM:
	case ETHTOOL_GSG:
	case ETHTOOL_GTSO:
	case ETHTOOL_GGSO:
	case ETHTOOL_GGRO:
		rc = ethtool_get_one_feature(dev, useraddr, ethcmd);
		break;
	case ETHTOOL_STXCSUM:
	case ETHTOOL_SRXCSUM:
	case ETHTOOL_SSG:
	case ETHTOOL_STSO:
	case ETHTOOL_SGSO:
	case ETHTOOL_SGRO:
		rc = ethtool_set_one_feature(dev, useraddr, ethcmd);
		break;
	case ETHTOOL_GCHANNELS:
		rc = ethtool_get_channels(dev, useraddr);
		break;
	case ETHTOOL_SCHANNELS:
		rc = ethtool_set_channels(dev, useraddr);
		break;
	case ETHTOOL_SET_DUMP:
		rc = ethtool_set_dump(dev, useraddr);
		break;
	case ETHTOOL_GET_DUMP_FLAG:
		rc = ethtool_get_dump_flag(dev, useraddr);
		break;
	case ETHTOOL_GET_DUMP_DATA:
		rc = ethtool_get_dump_data(dev, useraddr);
		break;
	case ETHTOOL_GET_TS_INFO:
		rc = ethtool_get_ts_info(dev, useraddr);
		break;
	case ETHTOOL_GMODULEINFO:
		rc = ethtool_get_module_info(dev, useraddr);
		break;
	case ETHTOOL_GMODULEEEPROM:
		rc = ethtool_get_module_eeprom(dev, useraddr);
		break;
	case ETHTOOL_GTUNABLE:
		rc = ethtool_get_tunable(dev, useraddr);
		break;
	case ETHTOOL_STUNABLE:
		rc = ethtool_set_tunable(dev, useraddr);
		break;
	case ETHTOOL_GPHYSTATS:
		rc = ethtool_get_phy_stats(dev, useraddr);
		break;
	case ETHTOOL_PERQUEUE:
		rc = ethtool_set_per_queue(dev, useraddr, sub_cmd);
		break;
	case ETHTOOL_GLINKSETTINGS:
		rc = ethtool_get_link_ksettings(dev, useraddr);
		break;
	case ETHTOOL_SLINKSETTINGS:
		rc = ethtool_set_link_ksettings(dev, useraddr);
		break;
	case ETHTOOL_PHY_GTUNABLE:
		rc = get_phy_tunable(dev, useraddr);
		break;
	case ETHTOOL_PHY_STUNABLE:
		rc = set_phy_tunable(dev, useraddr);
		break;
	case ETHTOOL_GFECPARAM:
		rc = ethtool_get_fecparam(dev, useraddr);
		break;
	case ETHTOOL_SFECPARAM:
		rc = ethtool_set_fecparam(dev, useraddr);
		break;
	default:
		rc = -EOPNOTSUPP;
	}

	if (dev->ethtool_ops->complete)
		dev->ethtool_ops->complete(dev);

	if (old_features != dev->features)
		netdev_features_change(dev);
out:
	if (dev->dev.parent)
		pm_runtime_put(dev->dev.parent);

	return rc;
}

int dev_ethtool(struct net *net, struct ifreq *ifr, void __user *useraddr)
{
	struct ethtool_devlink_compat *state;
	u32 ethcmd;
	int rc;

	if (copy_from_user(&ethcmd, useraddr, sizeof(ethcmd)))
		return -EFAULT;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	switch (ethcmd) {
	case ETHTOOL_FLASHDEV:
		if (copy_from_user(&state->efl, useraddr, sizeof(state->efl))) {
			rc = -EFAULT;
			goto exit_free;
		}
		state->efl.data[ETHTOOL_FLASH_MAX_FILENAME - 1] = 0;
		break;
	}

	rtnl_lock();
	rc = __dev_ethtool(net, ifr, useraddr, ethcmd, state);
	rtnl_unlock();
	if (rc)
		goto exit_free;

	switch (ethcmd) {
	case ETHTOOL_FLASHDEV:
		if (state->devlink)
			rc = devlink_compat_flash_update(state->devlink,
							 state->efl.data);
		break;
	case ETHTOOL_GDRVINFO:
		if (state->devlink)
			devlink_compat_running_version(state->devlink,
						       state->info.fw_version,
						       sizeof(state->info.fw_version));
		if (copy_to_user(useraddr, &state->info, sizeof(state->info))) {
			rc = -EFAULT;
			goto exit_free;
		}
		break;
	}

exit_free:
	if (state->devlink)
		devlink_put(state->devlink);
	kfree(state);
	return rc;
}

struct ethtool_rx_flow_key {
	struct flow_dissector_key_basic			basic;
	union {
		struct flow_dissector_key_ipv4_addrs	ipv4;
		struct flow_dissector_key_ipv6_addrs	ipv6;
	};
	struct flow_dissector_key_ports			tp;
	struct flow_dissector_key_ip			ip;
	struct flow_dissector_key_vlan			vlan;
	struct flow_dissector_key_eth_addrs		eth_addrs;
} __aligned(BITS_PER_LONG / 8); /* Ensure that we can do comparisons as longs. */

struct ethtool_rx_flow_match {
	struct flow_dissector		dissector;
	struct ethtool_rx_flow_key	key;
	struct ethtool_rx_flow_key	mask;
};

struct ethtool_rx_flow_rule *
ethtool_rx_flow_rule_create(const struct ethtool_rx_flow_spec_input *input)
{
	const struct ethtool_rx_flow_spec *fs = input->fs;
	struct ethtool_rx_flow_match *match;
	struct ethtool_rx_flow_rule *flow;
	struct flow_action_entry *act;

	flow = kzalloc(sizeof(struct ethtool_rx_flow_rule) +
		       sizeof(struct ethtool_rx_flow_match), GFP_KERNEL);
	if (!flow)
		return ERR_PTR(-ENOMEM);

	/* ethtool_rx supports only one single action per rule. */
	flow->rule = flow_rule_alloc(1);
	if (!flow->rule) {
		kfree(flow);
		return ERR_PTR(-ENOMEM);
	}

	match = (struct ethtool_rx_flow_match *)flow->priv;
	flow->rule->match.dissector	= &match->dissector;
	flow->rule->match.mask		= &match->mask;
	flow->rule->match.key		= &match->key;

	match->mask.basic.n_proto = htons(0xffff);

	switch (fs->flow_type & ~(FLOW_EXT | FLOW_MAC_EXT | FLOW_RSS)) {
	case ETHER_FLOW: {
		const struct ethhdr *ether_spec, *ether_m_spec;

		ether_spec = &fs->h_u.ether_spec;
		ether_m_spec = &fs->m_u.ether_spec;

		if (!is_zero_ether_addr(ether_m_spec->h_source)) {
			ether_addr_copy(match->key.eth_addrs.src,
					ether_spec->h_source);
			ether_addr_copy(match->mask.eth_addrs.src,
					ether_m_spec->h_source);
		}
		if (!is_zero_ether_addr(ether_m_spec->h_dest)) {
			ether_addr_copy(match->key.eth_addrs.dst,
					ether_spec->h_dest);
			ether_addr_copy(match->mask.eth_addrs.dst,
					ether_m_spec->h_dest);
		}
		if (ether_m_spec->h_proto) {
			match->key.basic.n_proto = ether_spec->h_proto;
			match->mask.basic.n_proto = ether_m_spec->h_proto;
		}
		}
		break;
	case TCP_V4_FLOW:
	case UDP_V4_FLOW: {
		const struct ethtool_tcpip4_spec *v4_spec, *v4_m_spec;

		match->key.basic.n_proto = htons(ETH_P_IP);

		v4_spec = &fs->h_u.tcp_ip4_spec;
		v4_m_spec = &fs->m_u.tcp_ip4_spec;

		if (v4_m_spec->ip4src) {
			match->key.ipv4.src = v4_spec->ip4src;
			match->mask.ipv4.src = v4_m_spec->ip4src;
		}
		if (v4_m_spec->ip4dst) {
			match->key.ipv4.dst = v4_spec->ip4dst;
			match->mask.ipv4.dst = v4_m_spec->ip4dst;
		}
		if (v4_m_spec->ip4src ||
		    v4_m_spec->ip4dst) {
			match->dissector.used_keys |=
				BIT(FLOW_DISSECTOR_KEY_IPV4_ADDRS);
			match->dissector.offset[FLOW_DISSECTOR_KEY_IPV4_ADDRS] =
				offsetof(struct ethtool_rx_flow_key, ipv4);
		}
		if (v4_m_spec->psrc) {
			match->key.tp.src = v4_spec->psrc;
			match->mask.tp.src = v4_m_spec->psrc;
		}
		if (v4_m_spec->pdst) {
			match->key.tp.dst = v4_spec->pdst;
			match->mask.tp.dst = v4_m_spec->pdst;
		}
		if (v4_m_spec->psrc ||
		    v4_m_spec->pdst) {
			match->dissector.used_keys |=
				BIT(FLOW_DISSECTOR_KEY_PORTS);
			match->dissector.offset[FLOW_DISSECTOR_KEY_PORTS] =
				offsetof(struct ethtool_rx_flow_key, tp);
		}
		if (v4_m_spec->tos) {
			match->key.ip.tos = v4_spec->tos;
			match->mask.ip.tos = v4_m_spec->tos;
			match->dissector.used_keys |=
				BIT(FLOW_DISSECTOR_KEY_IP);
			match->dissector.offset[FLOW_DISSECTOR_KEY_IP] =
				offsetof(struct ethtool_rx_flow_key, ip);
		}
		}
		break;
	case TCP_V6_FLOW:
	case UDP_V6_FLOW: {
		const struct ethtool_tcpip6_spec *v6_spec, *v6_m_spec;

		match->key.basic.n_proto = htons(ETH_P_IPV6);

		v6_spec = &fs->h_u.tcp_ip6_spec;
		v6_m_spec = &fs->m_u.tcp_ip6_spec;
		if (!ipv6_addr_any((struct in6_addr *)v6_m_spec->ip6src)) {
			memcpy(&match->key.ipv6.src, v6_spec->ip6src,
			       sizeof(match->key.ipv6.src));
			memcpy(&match->mask.ipv6.src, v6_m_spec->ip6src,
			       sizeof(match->mask.ipv6.src));
		}
		if (!ipv6_addr_any((struct in6_addr *)v6_m_spec->ip6dst)) {
			memcpy(&match->key.ipv6.dst, v6_spec->ip6dst,
			       sizeof(match->key.ipv6.dst));
			memcpy(&match->mask.ipv6.dst, v6_m_spec->ip6dst,
			       sizeof(match->mask.ipv6.dst));
		}
		if (!ipv6_addr_any((struct in6_addr *)v6_m_spec->ip6src) ||
		    !ipv6_addr_any((struct in6_addr *)v6_m_spec->ip6dst)) {
			match->dissector.used_keys |=
				BIT(FLOW_DISSECTOR_KEY_IPV6_ADDRS);
			match->dissector.offset[FLOW_DISSECTOR_KEY_IPV6_ADDRS] =
				offsetof(struct ethtool_rx_flow_key, ipv6);
		}
		if (v6_m_spec->psrc) {
			match->key.tp.src = v6_spec->psrc;
			match->mask.tp.src = v6_m_spec->psrc;
		}
		if (v6_m_spec->pdst) {
			match->key.tp.dst = v6_spec->pdst;
			match->mask.tp.dst = v6_m_spec->pdst;
		}
		if (v6_m_spec->psrc ||
		    v6_m_spec->pdst) {
			match->dissector.used_keys |=
				BIT(FLOW_DISSECTOR_KEY_PORTS);
			match->dissector.offset[FLOW_DISSECTOR_KEY_PORTS] =
				offsetof(struct ethtool_rx_flow_key, tp);
		}
		if (v6_m_spec->tclass) {
			match->key.ip.tos = v6_spec->tclass;
			match->mask.ip.tos = v6_m_spec->tclass;
			match->dissector.used_keys |=
				BIT(FLOW_DISSECTOR_KEY_IP);
			match->dissector.offset[FLOW_DISSECTOR_KEY_IP] =
				offsetof(struct ethtool_rx_flow_key, ip);
		}
		}
		break;
	default:
		ethtool_rx_flow_rule_destroy(flow);
		return ERR_PTR(-EINVAL);
	}

	switch (fs->flow_type & ~(FLOW_EXT | FLOW_MAC_EXT | FLOW_RSS)) {
	case TCP_V4_FLOW:
	case TCP_V6_FLOW:
		match->key.basic.ip_proto = IPPROTO_TCP;
		match->mask.basic.ip_proto = 0xff;
		break;
	case UDP_V4_FLOW:
	case UDP_V6_FLOW:
		match->key.basic.ip_proto = IPPROTO_UDP;
		match->mask.basic.ip_proto = 0xff;
		break;
	}

	match->dissector.used_keys |= BIT(FLOW_DISSECTOR_KEY_BASIC);
	match->dissector.offset[FLOW_DISSECTOR_KEY_BASIC] =
		offsetof(struct ethtool_rx_flow_key, basic);

	if (fs->flow_type & FLOW_EXT) {
		const struct ethtool_flow_ext *ext_h_spec = &fs->h_ext;
		const struct ethtool_flow_ext *ext_m_spec = &fs->m_ext;

		if (ext_m_spec->vlan_etype) {
			match->key.vlan.vlan_tpid = ext_h_spec->vlan_etype;
			match->mask.vlan.vlan_tpid = ext_m_spec->vlan_etype;
		}

		if (ext_m_spec->vlan_tci) {
			match->key.vlan.vlan_id =
				ntohs(ext_h_spec->vlan_tci) & 0x0fff;
			match->mask.vlan.vlan_id =
				ntohs(ext_m_spec->vlan_tci) & 0x0fff;

			match->key.vlan.vlan_dei =
				!!(ext_h_spec->vlan_tci & htons(0x1000));
			match->mask.vlan.vlan_dei =
				!!(ext_m_spec->vlan_tci & htons(0x1000));

			match->key.vlan.vlan_priority =
				(ntohs(ext_h_spec->vlan_tci) & 0xe000) >> 13;
			match->mask.vlan.vlan_priority =
				(ntohs(ext_m_spec->vlan_tci) & 0xe000) >> 13;
		}

		if (ext_m_spec->vlan_etype ||
		    ext_m_spec->vlan_tci) {
			match->dissector.used_keys |=
				BIT(FLOW_DISSECTOR_KEY_VLAN);
			match->dissector.offset[FLOW_DISSECTOR_KEY_VLAN] =
				offsetof(struct ethtool_rx_flow_key, vlan);
		}
	}
	if (fs->flow_type & FLOW_MAC_EXT) {
		const struct ethtool_flow_ext *ext_h_spec = &fs->h_ext;
		const struct ethtool_flow_ext *ext_m_spec = &fs->m_ext;

		memcpy(match->key.eth_addrs.dst, ext_h_spec->h_dest,
		       ETH_ALEN);
		memcpy(match->mask.eth_addrs.dst, ext_m_spec->h_dest,
		       ETH_ALEN);

		match->dissector.used_keys |=
			BIT(FLOW_DISSECTOR_KEY_ETH_ADDRS);
		match->dissector.offset[FLOW_DISSECTOR_KEY_ETH_ADDRS] =
			offsetof(struct ethtool_rx_flow_key, eth_addrs);
	}

	act = &flow->rule->action.entries[0];
	switch (fs->ring_cookie) {
	case RX_CLS_FLOW_DISC:
		act->id = FLOW_ACTION_DROP;
		break;
	case RX_CLS_FLOW_WAKE:
		act->id = FLOW_ACTION_WAKE;
		break;
	default:
		act->id = FLOW_ACTION_QUEUE;
		if (fs->flow_type & FLOW_RSS)
			act->queue.ctx = input->rss_ctx;

		act->queue.vf = ethtool_get_flow_spec_ring_vf(fs->ring_cookie);
		act->queue.index = ethtool_get_flow_spec_ring(fs->ring_cookie);
		break;
	}

	return flow;
}
EXPORT_SYMBOL(ethtool_rx_flow_rule_create);

void ethtool_rx_flow_rule_destroy(struct ethtool_rx_flow_rule *flow)
{
	kfree(flow->rule);
	kfree(flow);
}
EXPORT_SYMBOL(ethtool_rx_flow_rule_destroy);
