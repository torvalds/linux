/*
 * ibm_ocp.h
 *
 *      (c) Benjamin Herrenschmidt (benh@kernel.crashing.org)
 *          Mipsys - France
 *
 *          Derived from work (c) Armin Kuster akuster@pacbell.net
 *
 *          Additional support and port to 2.6 LDM/sysfs by
 *          Matt Porter <mporter@kernel.crashing.org>
 *          Copyright 2003-2004 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#ifdef __KERNEL__
#ifndef __IBM_OCP_H__
#define __IBM_OCP_H__

#include <asm/types.h>

/*
 * IBM 4xx OCP system information
 */
struct ocp_sys_info_data {
	int	opb_bus_freq;	/* OPB Bus Frequency (Hz) */
	int	ebc_bus_freq;	/* EBC Bus Frequency (Hz) */
};

extern struct ocp_sys_info_data ocp_sys_info;

/*
 * EMAC additional data and sysfs support
 *
 * Note about mdio_idx: When you have a zmii, it's usually
 * not necessary, it covers the case of the 405EP which has
 * the MDIO lines on EMAC0 only
 *
 * Note about phy_map: Per EMAC map of PHY ids which should
 * be probed by emac_probe. Different EMACs can have
 * overlapping maps.
 *
 * Note, this map uses inverse logic for bits:
 *  0 - id should be probed
 *  1 - id should be ignored
 *
 * Default value of 0x00000000 - will result in usual
 * auto-detection logic.
 *
 */

struct ocp_func_emac_data {
	int	rgmii_idx;	/* RGMII device index or -1 */
	int	rgmii_mux;	/* RGMII input of this EMAC */
	int	zmii_idx;	/* ZMII device index or -1 */
	int	zmii_mux;	/* ZMII input of this EMAC */
	int	mal_idx;	/* MAL device index */
	int	mal_rx_chan;	/* MAL rx channel number */
	int	mal_tx_chan;	/* MAL tx channel number */
	int	wol_irq;	/* WOL interrupt */
	int	mdio_idx;	/* EMAC idx of MDIO master or -1 */
	int	tah_idx;	/* TAH device index or -1 */
	int	jumbo;		/* Jumbo frames capable flag */
	int	phy_mode;	/* PHY type or configurable mode */
	u8	mac_addr[6];	/* EMAC mac address */
	u32	phy_map;	/* EMAC phy map */
	u32	phy_feat_exc;	/* Excluded PHY features */
};

/* Sysfs support */
#define OCP_SYSFS_EMAC_DATA()						\
OCP_SYSFS_ADDTL(struct ocp_func_emac_data, "%d\n", emac, rgmii_idx)	\
OCP_SYSFS_ADDTL(struct ocp_func_emac_data, "%d\n", emac, rgmii_mux)	\
OCP_SYSFS_ADDTL(struct ocp_func_emac_data, "%d\n", emac, zmii_idx)	\
OCP_SYSFS_ADDTL(struct ocp_func_emac_data, "%d\n", emac, zmii_mux)	\
OCP_SYSFS_ADDTL(struct ocp_func_emac_data, "%d\n", emac, mal_idx)	\
OCP_SYSFS_ADDTL(struct ocp_func_emac_data, "%d\n", emac, mal_rx_chan)	\
OCP_SYSFS_ADDTL(struct ocp_func_emac_data, "%d\n", emac, mal_tx_chan)	\
OCP_SYSFS_ADDTL(struct ocp_func_emac_data, "%d\n", emac, wol_irq)	\
OCP_SYSFS_ADDTL(struct ocp_func_emac_data, "%d\n", emac, mdio_idx)	\
OCP_SYSFS_ADDTL(struct ocp_func_emac_data, "%d\n", emac, tah_idx)	\
OCP_SYSFS_ADDTL(struct ocp_func_emac_data, "%d\n", emac, phy_mode)	\
OCP_SYSFS_ADDTL(struct ocp_func_emac_data, "0x%08x\n", emac, phy_map)	\
OCP_SYSFS_ADDTL(struct ocp_func_emac_data, "0x%08x\n", emac, phy_feat_exc)\
									\
void ocp_show_emac_data(struct device *dev)				\
{									\
	device_create_file(dev, &dev_attr_emac_rgmii_idx);		\
	device_create_file(dev, &dev_attr_emac_rgmii_mux);		\
	device_create_file(dev, &dev_attr_emac_zmii_idx);		\
	device_create_file(dev, &dev_attr_emac_zmii_mux);		\
	device_create_file(dev, &dev_attr_emac_mal_idx);		\
	device_create_file(dev, &dev_attr_emac_mal_rx_chan);		\
	device_create_file(dev, &dev_attr_emac_mal_tx_chan);		\
	device_create_file(dev, &dev_attr_emac_wol_irq);		\
	device_create_file(dev, &dev_attr_emac_mdio_idx);		\
	device_create_file(dev, &dev_attr_emac_tah_idx);		\
	device_create_file(dev, &dev_attr_emac_phy_mode);		\
	device_create_file(dev, &dev_attr_emac_phy_map);		\
	device_create_file(dev, &dev_attr_emac_phy_feat_exc);		\
}

/*
 * PHY mode settings (EMAC <-> ZMII/RGMII bridge <-> PHY)
 */
#define PHY_MODE_NA	0
#define PHY_MODE_MII	1
#define PHY_MODE_RMII	2
#define PHY_MODE_SMII	3
#define PHY_MODE_RGMII	4
#define PHY_MODE_TBI	5
#define PHY_MODE_GMII	6
#define PHY_MODE_RTBI	7
#define PHY_MODE_SGMII	8

#ifdef CONFIG_40x
/*
 * Helper function to copy MAC addresses from the bd_t to OCP EMAC
 * additions.
 *
 * The range of EMAC indices (inclusive) to be copied are the arguments.
 */
static inline void ibm_ocp_set_emac(int start, int end)
{
	int i;
	struct ocp_def *def;

	/* Copy MAC addresses to EMAC additions */
	for (i=start; i<=end; i++) {
		def = ocp_get_one_device(OCP_VENDOR_IBM, OCP_FUNC_EMAC, i);
		memcpy(((struct ocp_func_emac_data *)def->additions)->mac_addr,
				&__res.bi_enetaddr[i],
				6);
	}
}
#endif

/*
 * MAL additional data and sysfs support
 */
struct ocp_func_mal_data {
	int	num_tx_chans;	/* Number of TX channels */
	int	num_rx_chans;	/* Number of RX channels */
	int 	txeob_irq;	/* TX End Of Buffer IRQ  */
	int 	rxeob_irq;	/* RX End Of Buffer IRQ  */
	int	txde_irq;	/* TX Descriptor Error IRQ */
	int	rxde_irq;	/* RX Descriptor Error IRQ */
	int	serr_irq;	/* MAL System Error IRQ    */
	int	dcr_base;	/* MALx_CFG DCR number   */
};

#define OCP_SYSFS_MAL_DATA()						\
OCP_SYSFS_ADDTL(struct ocp_func_mal_data, "%d\n", mal, num_tx_chans)	\
OCP_SYSFS_ADDTL(struct ocp_func_mal_data, "%d\n", mal, num_rx_chans)	\
OCP_SYSFS_ADDTL(struct ocp_func_mal_data, "%d\n", mal, txeob_irq)	\
OCP_SYSFS_ADDTL(struct ocp_func_mal_data, "%d\n", mal, rxeob_irq)	\
OCP_SYSFS_ADDTL(struct ocp_func_mal_data, "%d\n", mal, txde_irq)	\
OCP_SYSFS_ADDTL(struct ocp_func_mal_data, "%d\n", mal, rxde_irq)	\
OCP_SYSFS_ADDTL(struct ocp_func_mal_data, "%d\n", mal, serr_irq)	\
OCP_SYSFS_ADDTL(struct ocp_func_mal_data, "%d\n", mal, dcr_base)	\
									\
void ocp_show_mal_data(struct device *dev)				\
{									\
	device_create_file(dev, &dev_attr_mal_num_tx_chans);		\
	device_create_file(dev, &dev_attr_mal_num_rx_chans);		\
	device_create_file(dev, &dev_attr_mal_txeob_irq);		\
	device_create_file(dev, &dev_attr_mal_rxeob_irq);		\
	device_create_file(dev, &dev_attr_mal_txde_irq);		\
	device_create_file(dev, &dev_attr_mal_rxde_irq);		\
	device_create_file(dev, &dev_attr_mal_serr_irq);		\
	device_create_file(dev, &dev_attr_mal_dcr_base);		\
}

/*
 * IIC additional data and sysfs support
 */
struct ocp_func_iic_data {
	int	fast_mode;	/* IIC fast mode enabled */
};

#define OCP_SYSFS_IIC_DATA()						\
OCP_SYSFS_ADDTL(struct ocp_func_iic_data, "%d\n", iic, fast_mode)	\
									\
void ocp_show_iic_data(struct device *dev)				\
{									\
	device_create_file(dev, &dev_attr_iic_fast_mode);		\
}
#endif /* __IBM_OCP_H__ */
#endif /* __KERNEL__ */
