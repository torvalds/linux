/* SPDX-License-Identifier: GPL-2.0 */
/*
 * linux/mii.h: definitions for MII-compatible transceivers
 * Originally drivers/net/sunhme.h.
 *
 * Copyright (C) 1996, 1999, 2001 David S. Miller (davem@redhat.com)
 */
#ifndef __LINUX_MII_H__
#define __LINUX_MII_H__


#include <linux/if.h>
#include <linux/linkmode.h>
#include <uapi/linux/mii.h>

struct ethtool_cmd;

struct mii_if_info {
	int phy_id;
	int advertising;
	int phy_id_mask;
	int reg_num_mask;

	unsigned int full_duplex : 1;	/* is full duplex? */
	unsigned int force_media : 1;	/* is autoneg. disabled? */
	unsigned int supports_gmii : 1; /* are GMII registers supported? */

	struct net_device *dev;
	int (*mdio_read) (struct net_device *dev, int phy_id, int location);
	void (*mdio_write) (struct net_device *dev, int phy_id, int location, int val);
};

extern int mii_link_ok (struct mii_if_info *mii);
extern int mii_nway_restart (struct mii_if_info *mii);
extern int mii_ethtool_gset(struct mii_if_info *mii, struct ethtool_cmd *ecmd);
extern void mii_ethtool_get_link_ksettings(
	struct mii_if_info *mii, struct ethtool_link_ksettings *cmd);
extern int mii_ethtool_sset(struct mii_if_info *mii, struct ethtool_cmd *ecmd);
extern int mii_ethtool_set_link_ksettings(
	struct mii_if_info *mii, const struct ethtool_link_ksettings *cmd);
extern int mii_check_gmii_support(struct mii_if_info *mii);
extern void mii_check_link (struct mii_if_info *mii);
extern unsigned int mii_check_media (struct mii_if_info *mii,
				     unsigned int ok_to_print,
				     unsigned int init_media);
extern int generic_mii_ioctl(struct mii_if_info *mii_if,
			     struct mii_ioctl_data *mii_data, int cmd,
			     unsigned int *duplex_changed);


static inline struct mii_ioctl_data *if_mii(struct ifreq *rq)
{
	return (struct mii_ioctl_data *) &rq->ifr_ifru;
}

/**
 * mii_nway_result
 * @negotiated: value of MII ANAR and'd with ANLPAR
 *
 * Given a set of MII abilities, check each bit and returns the
 * currently supported media, in the priority order defined by
 * IEEE 802.3u.  We use LPA_xxx constants but note this is not the
 * value of LPA solely, as described above.
 *
 * The one exception to IEEE 802.3u is that 100baseT4 is placed
 * between 100T-full and 100T-half.  If your phy does not support
 * 100T4 this is fine.  If your phy places 100T4 elsewhere in the
 * priority order, you will need to roll your own function.
 */
static inline unsigned int mii_nway_result (unsigned int negotiated)
{
	unsigned int ret;

	if (negotiated & LPA_100FULL)
		ret = LPA_100FULL;
	else if (negotiated & LPA_100BASE4)
		ret = LPA_100BASE4;
	else if (negotiated & LPA_100HALF)
		ret = LPA_100HALF;
	else if (negotiated & LPA_10FULL)
		ret = LPA_10FULL;
	else
		ret = LPA_10HALF;

	return ret;
}

/**
 * mii_duplex
 * @duplex_lock: Non-zero if duplex is locked at full
 * @negotiated: value of MII ANAR and'd with ANLPAR
 *
 * A small helper function for a common case.  Returns one
 * if the media is operating or locked at full duplex, and
 * returns zero otherwise.
 */
static inline unsigned int mii_duplex (unsigned int duplex_lock,
				       unsigned int negotiated)
{
	if (duplex_lock)
		return 1;
	if (mii_nway_result(negotiated) & LPA_DUPLEX)
		return 1;
	return 0;
}

/**
 * ethtool_adv_to_mii_adv_t
 * @ethadv: the ethtool advertisement settings
 *
 * A small helper function that translates ethtool advertisement
 * settings to phy autonegotiation advertisements for the
 * MII_ADVERTISE register.
 */
static inline u32 ethtool_adv_to_mii_adv_t(u32 ethadv)
{
	u32 result = 0;

	if (ethadv & ADVERTISED_10baseT_Half)
		result |= ADVERTISE_10HALF;
	if (ethadv & ADVERTISED_10baseT_Full)
		result |= ADVERTISE_10FULL;
	if (ethadv & ADVERTISED_100baseT_Half)
		result |= ADVERTISE_100HALF;
	if (ethadv & ADVERTISED_100baseT_Full)
		result |= ADVERTISE_100FULL;
	if (ethadv & ADVERTISED_Pause)
		result |= ADVERTISE_PAUSE_CAP;
	if (ethadv & ADVERTISED_Asym_Pause)
		result |= ADVERTISE_PAUSE_ASYM;

	return result;
}

/**
 * linkmode_adv_to_mii_adv_t
 * @advertising: the linkmode advertisement settings
 *
 * A small helper function that translates linkmode advertisement
 * settings to phy autonegotiation advertisements for the
 * MII_ADVERTISE register.
 */
static inline u32 linkmode_adv_to_mii_adv_t(unsigned long *advertising)
{
	u32 result = 0;

	if (linkmode_test_bit(ETHTOOL_LINK_MODE_10baseT_Half_BIT, advertising))
		result |= ADVERTISE_10HALF;
	if (linkmode_test_bit(ETHTOOL_LINK_MODE_10baseT_Full_BIT, advertising))
		result |= ADVERTISE_10FULL;
	if (linkmode_test_bit(ETHTOOL_LINK_MODE_100baseT_Half_BIT, advertising))
		result |= ADVERTISE_100HALF;
	if (linkmode_test_bit(ETHTOOL_LINK_MODE_100baseT_Full_BIT, advertising))
		result |= ADVERTISE_100FULL;
	if (linkmode_test_bit(ETHTOOL_LINK_MODE_Pause_BIT, advertising))
		result |= ADVERTISE_PAUSE_CAP;
	if (linkmode_test_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT, advertising))
		result |= ADVERTISE_PAUSE_ASYM;

	return result;
}

/**
 * mii_adv_to_ethtool_adv_t
 * @adv: value of the MII_ADVERTISE register
 *
 * A small helper function that translates MII_ADVERTISE bits
 * to ethtool advertisement settings.
 */
static inline u32 mii_adv_to_ethtool_adv_t(u32 adv)
{
	u32 result = 0;

	if (adv & ADVERTISE_10HALF)
		result |= ADVERTISED_10baseT_Half;
	if (adv & ADVERTISE_10FULL)
		result |= ADVERTISED_10baseT_Full;
	if (adv & ADVERTISE_100HALF)
		result |= ADVERTISED_100baseT_Half;
	if (adv & ADVERTISE_100FULL)
		result |= ADVERTISED_100baseT_Full;
	if (adv & ADVERTISE_PAUSE_CAP)
		result |= ADVERTISED_Pause;
	if (adv & ADVERTISE_PAUSE_ASYM)
		result |= ADVERTISED_Asym_Pause;

	return result;
}

/**
 * ethtool_adv_to_mii_ctrl1000_t
 * @ethadv: the ethtool advertisement settings
 *
 * A small helper function that translates ethtool advertisement
 * settings to phy autonegotiation advertisements for the
 * MII_CTRL1000 register when in 1000T mode.
 */
static inline u32 ethtool_adv_to_mii_ctrl1000_t(u32 ethadv)
{
	u32 result = 0;

	if (ethadv & ADVERTISED_1000baseT_Half)
		result |= ADVERTISE_1000HALF;
	if (ethadv & ADVERTISED_1000baseT_Full)
		result |= ADVERTISE_1000FULL;

	return result;
}

/**
 * linkmode_adv_to_mii_ctrl1000_t
 * @advertising: the linkmode advertisement settings
 *
 * A small helper function that translates linkmode advertisement
 * settings to phy autonegotiation advertisements for the
 * MII_CTRL1000 register when in 1000T mode.
 */
static inline u32 linkmode_adv_to_mii_ctrl1000_t(unsigned long *advertising)
{
	u32 result = 0;

	if (linkmode_test_bit(ETHTOOL_LINK_MODE_1000baseT_Half_BIT,
			      advertising))
		result |= ADVERTISE_1000HALF;
	if (linkmode_test_bit(ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
			      advertising))
		result |= ADVERTISE_1000FULL;

	return result;
}

/**
 * mii_ctrl1000_to_ethtool_adv_t
 * @adv: value of the MII_CTRL1000 register
 *
 * A small helper function that translates MII_CTRL1000
 * bits, when in 1000Base-T mode, to ethtool
 * advertisement settings.
 */
static inline u32 mii_ctrl1000_to_ethtool_adv_t(u32 adv)
{
	u32 result = 0;

	if (adv & ADVERTISE_1000HALF)
		result |= ADVERTISED_1000baseT_Half;
	if (adv & ADVERTISE_1000FULL)
		result |= ADVERTISED_1000baseT_Full;

	return result;
}

/**
 * mii_lpa_to_ethtool_lpa_t
 * @adv: value of the MII_LPA register
 *
 * A small helper function that translates MII_LPA
 * bits, when in 1000Base-T mode, to ethtool
 * LP advertisement settings.
 */
static inline u32 mii_lpa_to_ethtool_lpa_t(u32 lpa)
{
	u32 result = 0;

	if (lpa & LPA_LPACK)
		result |= ADVERTISED_Autoneg;

	return result | mii_adv_to_ethtool_adv_t(lpa);
}

/**
 * mii_stat1000_to_ethtool_lpa_t
 * @adv: value of the MII_STAT1000 register
 *
 * A small helper function that translates MII_STAT1000
 * bits, when in 1000Base-T mode, to ethtool
 * advertisement settings.
 */
static inline u32 mii_stat1000_to_ethtool_lpa_t(u32 lpa)
{
	u32 result = 0;

	if (lpa & LPA_1000HALF)
		result |= ADVERTISED_1000baseT_Half;
	if (lpa & LPA_1000FULL)
		result |= ADVERTISED_1000baseT_Full;

	return result;
}

/**
 * mii_stat1000_mod_linkmode_lpa_t
 * @advertising: target the linkmode advertisement settings
 * @adv: value of the MII_STAT1000 register
 *
 * A small helper function that translates MII_STAT1000 bits, when in
 * 1000Base-T mode, to linkmode advertisement settings. Other bits in
 * advertising are not changes.
 */
static inline void mii_stat1000_mod_linkmode_lpa_t(unsigned long *advertising,
						   u32 lpa)
{
	linkmode_mod_bit(ETHTOOL_LINK_MODE_1000baseT_Half_BIT,
			 advertising, lpa & LPA_1000HALF);

	linkmode_mod_bit(ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
			 advertising, lpa & LPA_1000FULL);
}

/**
 * ethtool_adv_to_mii_adv_x
 * @ethadv: the ethtool advertisement settings
 *
 * A small helper function that translates ethtool advertisement
 * settings to phy autonegotiation advertisements for the
 * MII_CTRL1000 register when in 1000Base-X mode.
 */
static inline u32 ethtool_adv_to_mii_adv_x(u32 ethadv)
{
	u32 result = 0;

	if (ethadv & ADVERTISED_1000baseT_Half)
		result |= ADVERTISE_1000XHALF;
	if (ethadv & ADVERTISED_1000baseT_Full)
		result |= ADVERTISE_1000XFULL;
	if (ethadv & ADVERTISED_Pause)
		result |= ADVERTISE_1000XPAUSE;
	if (ethadv & ADVERTISED_Asym_Pause)
		result |= ADVERTISE_1000XPSE_ASYM;

	return result;
}

/**
 * mii_adv_to_ethtool_adv_x
 * @adv: value of the MII_CTRL1000 register
 *
 * A small helper function that translates MII_CTRL1000
 * bits, when in 1000Base-X mode, to ethtool
 * advertisement settings.
 */
static inline u32 mii_adv_to_ethtool_adv_x(u32 adv)
{
	u32 result = 0;

	if (adv & ADVERTISE_1000XHALF)
		result |= ADVERTISED_1000baseT_Half;
	if (adv & ADVERTISE_1000XFULL)
		result |= ADVERTISED_1000baseT_Full;
	if (adv & ADVERTISE_1000XPAUSE)
		result |= ADVERTISED_Pause;
	if (adv & ADVERTISE_1000XPSE_ASYM)
		result |= ADVERTISED_Asym_Pause;

	return result;
}

/**
 * mii_lpa_to_ethtool_lpa_x
 * @adv: value of the MII_LPA register
 *
 * A small helper function that translates MII_LPA
 * bits, when in 1000Base-X mode, to ethtool
 * LP advertisement settings.
 */
static inline u32 mii_lpa_to_ethtool_lpa_x(u32 lpa)
{
	u32 result = 0;

	if (lpa & LPA_LPACK)
		result |= ADVERTISED_Autoneg;

	return result | mii_adv_to_ethtool_adv_x(lpa);
}

/**
 * mii_lpa_mod_linkmode_adv_sgmii
 * @lp_advertising: pointer to destination link mode.
 * @lpa: value of the MII_LPA register
 *
 * A small helper function that translates MII_LPA bits to
 * linkmode advertisement settings for SGMII.
 * Leaves other bits unchanged.
 */
static inline void
mii_lpa_mod_linkmode_lpa_sgmii(unsigned long *lp_advertising, u32 lpa)
{
	u32 speed_duplex = lpa & LPA_SGMII_DPX_SPD_MASK;

	linkmode_mod_bit(ETHTOOL_LINK_MODE_1000baseT_Half_BIT, lp_advertising,
			 speed_duplex == LPA_SGMII_1000HALF);

	linkmode_mod_bit(ETHTOOL_LINK_MODE_1000baseT_Full_BIT, lp_advertising,
			 speed_duplex == LPA_SGMII_1000FULL);

	linkmode_mod_bit(ETHTOOL_LINK_MODE_100baseT_Half_BIT, lp_advertising,
			 speed_duplex == LPA_SGMII_100HALF);

	linkmode_mod_bit(ETHTOOL_LINK_MODE_100baseT_Full_BIT, lp_advertising,
			 speed_duplex == LPA_SGMII_100FULL);

	linkmode_mod_bit(ETHTOOL_LINK_MODE_10baseT_Half_BIT, lp_advertising,
			 speed_duplex == LPA_SGMII_10HALF);

	linkmode_mod_bit(ETHTOOL_LINK_MODE_10baseT_Full_BIT, lp_advertising,
			 speed_duplex == LPA_SGMII_10FULL);
}

/**
 * mii_lpa_to_linkmode_adv_sgmii
 * @advertising: pointer to destination link mode.
 * @lpa: value of the MII_LPA register
 *
 * A small helper function that translates MII_ADVERTISE bits
 * to linkmode advertisement settings when in SGMII mode.
 * Clears the old value of advertising.
 */
static inline void mii_lpa_to_linkmode_lpa_sgmii(unsigned long *lp_advertising,
						 u32 lpa)
{
	linkmode_zero(lp_advertising);

	mii_lpa_mod_linkmode_lpa_sgmii(lp_advertising, lpa);
}

/**
 * mii_adv_mod_linkmode_adv_t
 * @advertising:pointer to destination link mode.
 * @adv: value of the MII_ADVERTISE register
 *
 * A small helper function that translates MII_ADVERTISE bits to
 * linkmode advertisement settings. Leaves other bits unchanged.
 */
static inline void mii_adv_mod_linkmode_adv_t(unsigned long *advertising,
					      u32 adv)
{
	linkmode_mod_bit(ETHTOOL_LINK_MODE_10baseT_Half_BIT,
			 advertising, adv & ADVERTISE_10HALF);

	linkmode_mod_bit(ETHTOOL_LINK_MODE_10baseT_Full_BIT,
			 advertising, adv & ADVERTISE_10FULL);

	linkmode_mod_bit(ETHTOOL_LINK_MODE_100baseT_Half_BIT,
			 advertising, adv & ADVERTISE_100HALF);

	linkmode_mod_bit(ETHTOOL_LINK_MODE_100baseT_Full_BIT,
			 advertising, adv & ADVERTISE_100FULL);

	linkmode_mod_bit(ETHTOOL_LINK_MODE_Pause_BIT, advertising,
			 adv & ADVERTISE_PAUSE_CAP);

	linkmode_mod_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT,
			 advertising, adv & ADVERTISE_PAUSE_ASYM);
}

/**
 * mii_adv_to_linkmode_adv_t
 * @advertising:pointer to destination link mode.
 * @adv: value of the MII_ADVERTISE register
 *
 * A small helper function that translates MII_ADVERTISE bits
 * to linkmode advertisement settings. Clears the old value
 * of advertising.
 */
static inline void mii_adv_to_linkmode_adv_t(unsigned long *advertising,
					     u32 adv)
{
	linkmode_zero(advertising);

	mii_adv_mod_linkmode_adv_t(advertising, adv);
}

/**
 * mii_lpa_to_linkmode_lpa_t
 * @adv: value of the MII_LPA register
 *
 * A small helper function that translates MII_LPA bits, when in
 * 1000Base-T mode, to linkmode LP advertisement settings. Clears the
 * old value of advertising
 */
static inline void mii_lpa_to_linkmode_lpa_t(unsigned long *lp_advertising,
					     u32 lpa)
{
	mii_adv_to_linkmode_adv_t(lp_advertising, lpa);

	if (lpa & LPA_LPACK)
		linkmode_set_bit(ETHTOOL_LINK_MODE_Autoneg_BIT,
				 lp_advertising);

}

/**
 * mii_lpa_mod_linkmode_lpa_t
 * @adv: value of the MII_LPA register
 *
 * A small helper function that translates MII_LPA bits, when in
 * 1000Base-T mode, to linkmode LP advertisement settings. Leaves
 * other bits unchanged.
 */
static inline void mii_lpa_mod_linkmode_lpa_t(unsigned long *lp_advertising,
					      u32 lpa)
{
	mii_adv_mod_linkmode_adv_t(lp_advertising, lpa);

	linkmode_mod_bit(ETHTOOL_LINK_MODE_Autoneg_BIT,
			 lp_advertising, lpa & LPA_LPACK);
}

static inline void mii_ctrl1000_mod_linkmode_adv_t(unsigned long *advertising,
						   u32 ctrl1000)
{
	linkmode_mod_bit(ETHTOOL_LINK_MODE_1000baseT_Half_BIT, advertising,
			 ctrl1000 & ADVERTISE_1000HALF);
	linkmode_mod_bit(ETHTOOL_LINK_MODE_1000baseT_Full_BIT, advertising,
			 ctrl1000 & ADVERTISE_1000FULL);
}

/**
 * linkmode_adv_to_lcl_adv_t
 * @advertising:pointer to linkmode advertising
 *
 * A small helper function that translates linkmode advertising to LVL
 * pause capabilities.
 */
static inline u32 linkmode_adv_to_lcl_adv_t(unsigned long *advertising)
{
	u32 lcl_adv = 0;

	if (linkmode_test_bit(ETHTOOL_LINK_MODE_Pause_BIT,
			      advertising))
		lcl_adv |= ADVERTISE_PAUSE_CAP;
	if (linkmode_test_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT,
			      advertising))
		lcl_adv |= ADVERTISE_PAUSE_ASYM;

	return lcl_adv;
}

/**
 * mii_advertise_flowctrl - get flow control advertisement flags
 * @cap: Flow control capabilities (FLOW_CTRL_RX, FLOW_CTRL_TX or both)
 */
static inline u16 mii_advertise_flowctrl(int cap)
{
	u16 adv = 0;

	if (cap & FLOW_CTRL_RX)
		adv = ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM;
	if (cap & FLOW_CTRL_TX)
		adv ^= ADVERTISE_PAUSE_ASYM;

	return adv;
}

/**
 * mii_resolve_flowctrl_fdx
 * @lcladv: value of MII ADVERTISE register
 * @rmtadv: value of MII LPA register
 *
 * Resolve full duplex flow control as per IEEE 802.3-2005 table 28B-3
 */
static inline u8 mii_resolve_flowctrl_fdx(u16 lcladv, u16 rmtadv)
{
	u8 cap = 0;

	if (lcladv & rmtadv & ADVERTISE_PAUSE_CAP) {
		cap = FLOW_CTRL_TX | FLOW_CTRL_RX;
	} else if (lcladv & rmtadv & ADVERTISE_PAUSE_ASYM) {
		if (lcladv & ADVERTISE_PAUSE_CAP)
			cap = FLOW_CTRL_RX;
		else if (rmtadv & ADVERTISE_PAUSE_CAP)
			cap = FLOW_CTRL_TX;
	}

	return cap;
}

#endif /* __LINUX_MII_H__ */
