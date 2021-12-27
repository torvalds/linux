/*
 * TC956X ethernet driver.
 *
 * tc956xmac_mdio.c
 *
 * Copyright (C) 2007-2009  STMicroelectronics Ltd
 * Copyright (C) 2021 Toshiba Electronic Devices & Storage Corporation
 *
 * This file has been derived from the STMicro Linux driver,
 * and developed or modified for TC956X.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*! History:
 *  20 Jan 2021 : Initial Version
 *  VERSION     : 00-01
 *
 *  15 Mar 2021 : Base lined
 *  VERSION     : 01-00
 *
 *  05 Jul 2021 : 1. Used Systick handler instead of Driver kernel timer to process transmitted Tx descriptors.
 *                2. XFI interface support and module parameters for selection of Port0 and Port1 interface
 *  VERSION     : 01-00-01
 *  20 Jul 2021 : 1. MAX C22 address changed to 3. Print not corrected for C45 PHY selection
 *  VERSION     : 01-00-03
 *  24 Nov 2021 : 1. Restricted MDIO access when no PHY found or MDIO registration fails
 *                2. Added mdio lock for making mii bus of private member to null to avoid parallel accessing to MDIO bus
 *  VERSION     : 01-00-23
 *  03 Dec 2021 : 1. Max C22/C45 PHY address changed to PHY_MAX_ADDR.
 *  VERSION     : 01-00-29
 *  27 Dec 2021 : 1. Initialisation of mii private variable.
 *  VERSION     : 01-00-32
 */

#include <linux/gpio/consumer.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/mii.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/property.h>
#include <linux/slab.h>

#include "dwxgmac2.h"
#include "tc956xmac.h"

#define MII_BUSY 0x00000001
#define MII_WRITE 0x00000002
#define MII_DATA_MASK GENMASK(15, 0)

/* GMAC4 defines */
#define MII_GMAC4_GOC_SHIFT		2
#define MII_GMAC4_REG_ADDR_SHIFT	16
#define MII_GMAC4_WRITE		(1 << MII_GMAC4_GOC_SHIFT)
#define MII_GMAC4_READ			(3 << MII_GMAC4_GOC_SHIFT)
#define MII_GMAC4_C45E			BIT(1)

/* XGMAC defines */
#define MII_XGMAC_SADDR			BIT(18)
#define MII_XGMAC_CMD_SHIFT		16
#define MII_XGMAC_WRITE			(1 << MII_XGMAC_CMD_SHIFT)
#define MII_XGMAC_READ			(3 << MII_XGMAC_CMD_SHIFT)
#define MII_XGMAC_BUSY			BIT(22)
#define MII_XGMAC_MAX_C22ADDR		3
#define MII_XGMAC_C22P_MASK		GENMASK(MII_XGMAC_MAX_C22ADDR, 0)
#define MII_XGMAC_PA_SHIFT		16
#define MII_XGMAC_DA_SHIFT		21
#define MII_XGMAC_CRS			BIT(31)
#define MII_XGMAC_CRS_SHIFT		31

static int tc956xmac_xgmac2_c45_format(struct tc956xmac_priv *priv, int phyaddr,
				    int phyreg, u32 *hw_addr)
{
	u32 tmp;

	/* Set port as Clause 45 */
	tmp = readl(priv->ioaddr + XGMAC_MDIO_C22P);
	tmp &= ~BIT(phyaddr);
	writel(tmp, priv->ioaddr + XGMAC_MDIO_C22P);

	*hw_addr = (phyaddr << MII_XGMAC_PA_SHIFT) | (phyreg & 0xffff);
	*hw_addr |= (phyreg >> MII_DEVADDR_C45_SHIFT) << MII_XGMAC_DA_SHIFT;
	return 0;
}

static int tc956xmac_xgmac2_c22_format(struct tc956xmac_priv *priv, int phyaddr,
				    int phyreg, u32 *hw_addr)
{
	u32 tmp;

	/* HW does not support C22 addr >= 4 */
	//if (phyaddr > MII_XGMAC_MAX_C22ADDR)
		//return -ENODEV;

	/* Set port as Clause 22 */
	tmp = readl(priv->ioaddr + XGMAC_MDIO_C22P);
	//tmp &= ~MII_XGMAC_C22P_MASK;
	tmp |= BIT(phyaddr);
	writel(tmp, priv->ioaddr + XGMAC_MDIO_C22P);

	*hw_addr = (phyaddr << MII_XGMAC_PA_SHIFT) | (phyreg & 0x1f);
	return 0;
}

static int __tc956xmac_xgmac2_mdio_read(struct mii_bus *bus, int phyaddr, int phyreg)
{
	struct net_device *ndev = bus->priv;
	struct tc956xmac_priv *priv = netdev_priv(ndev);
	unsigned int mii_address = priv->hw->mii.addr;
	unsigned int mii_data = priv->hw->mii.data;
	u32 tmp, addr, value = MII_XGMAC_BUSY;
	int ret;

	if (priv->plat->cphy_read)
		return priv->plat->cphy_read(priv, phyaddr, phyreg);

	/* Wait until any existing MII operation is complete */
	if (readl_poll_timeout(priv->ioaddr + mii_data, tmp,
			       !(tmp & MII_XGMAC_BUSY), /*100*/1, 10000))
		return -EBUSY;

	if ((priv->plat->c45_needed == true) && (phyreg < 0x1F)) {
		if (phyreg == 0)
			phyreg = (MII_ADDR_C45 | ((PHY_CL45_CTRL_REG_MMD_BANK) << 16) | (PHY_CL45_CTRL_REG_ADDR));
		else if (phyreg == 1)
			phyreg = (MII_ADDR_C45 | ((PHY_CL45_STATUS_REG_MMD_BANK) << 16) | (PHY_CL45_STATUS_REG_ADDR));
		else if (phyreg == 2)
			phyreg = (MII_ADDR_C45 | ((PHY_CL45_PHYID1_MMD_BANK) << 16) | (PHY_CL45_PHYID1_ADDR));
		else if (phyreg == 3)
			phyreg = (MII_ADDR_C45 | ((PHY_CL45_PHYID2_MMD_BANK) << 16) | (PHY_CL45_PHYID2_ADDR));
		else
			netdev_dbg(priv->dev, "%s Clause 45 register not defined for PHY register 0x%x\n", __func__, phyreg);

	}


	if (phyreg & MII_ADDR_C45) {
		phyreg &= ~MII_ADDR_C45;

		ret = tc956xmac_xgmac2_c45_format(priv, phyaddr, phyreg, &addr);
		if (ret)
			return ret;
	} else {
		ret = tc956xmac_xgmac2_c22_format(priv, phyaddr, phyreg, &addr);
		if (ret)
			return ret;

		value |= MII_XGMAC_SADDR;
	}

	value |= (priv->clk_csr << priv->hw->mii.clk_csr_shift)
		& priv->hw->mii.clk_csr_mask;

	value &= ~MII_XGMAC_CRS;
	value |= (priv->plat->clk_crs << MII_XGMAC_CRS_SHIFT);

	value |= MII_XGMAC_READ;

	/* Wait until any existing MII operation is complete */
	if (readl_poll_timeout(priv->ioaddr + mii_data, tmp,
			       !(tmp & MII_XGMAC_BUSY), /*100*/1, 10000))
		return -EBUSY;

	/* Set the MII address register to read */
	writel(addr, priv->ioaddr + mii_address);
	writel(value, priv->ioaddr + mii_data);

	/* Wait until any existing MII operation is complete */
	if (readl_poll_timeout(priv->ioaddr + mii_data, tmp,
			       !(tmp & MII_XGMAC_BUSY), /*100*/10, 10000))
		return -EBUSY;

	/* Read the data from the MII data register */
	return readl(priv->ioaddr + mii_data) & GENMASK(15, 0);
}
/**
 * __tc956xmac_xgmac2_mdio_read
 * @bus: points to the mii_bus structure
 * @phyaddr: MII addr
 * @phyreg: MII reg
 * Description: Check whether MDIO bus is registered successfully or not
 * if registered then access MDIO for Read operation
 */
static int tc956xmac_xgmac2_mdio_read(struct mii_bus *bus, int phyaddr, int phyreg)
{
	return bus->priv ?
		__tc956xmac_xgmac2_mdio_read(bus, phyaddr, phyreg) : -EIO;
}

static int __tc956xmac_xgmac2_mdio_write(struct mii_bus *bus, int phyaddr,
				    int phyreg, u16 phydata)
{
	struct net_device *ndev = bus->priv;
	struct tc956xmac_priv *priv = netdev_priv(ndev);
	unsigned int mii_address = priv->hw->mii.addr;
	unsigned int mii_data = priv->hw->mii.data;
	u32 addr, tmp, value = MII_XGMAC_BUSY;
	int ret;

	if (priv->plat->cphy_write)
		return priv->plat->cphy_write(priv, phyaddr, phyreg, phydata);

	/* Wait until any existing MII operation is complete */
	if (readl_poll_timeout(priv->ioaddr + mii_data, tmp,
			       !(tmp & MII_XGMAC_BUSY), /*100*/1, 10000))
		return -EBUSY;

	if (phyreg & MII_ADDR_C45) {
		phyreg &= ~MII_ADDR_C45;

		ret = tc956xmac_xgmac2_c45_format(priv, phyaddr, phyreg, &addr);
		if (ret)
			return ret;
	} else {
		ret = tc956xmac_xgmac2_c22_format(priv, phyaddr, phyreg, &addr);
		if (ret)
			return ret;

		value |= MII_XGMAC_SADDR;
	}

	value |= (priv->clk_csr << priv->hw->mii.clk_csr_shift)
		& priv->hw->mii.clk_csr_mask;

	value &= ~MII_XGMAC_CRS;
	value |= (priv->plat->clk_crs << MII_XGMAC_CRS_SHIFT);

	value |= phydata;
	value |= MII_XGMAC_WRITE;

	/* Wait until any existing MII operation is complete */
	if (readl_poll_timeout(priv->ioaddr + mii_data, tmp,
			       !(tmp & MII_XGMAC_BUSY), /*100*/1, 10000))
		return -EBUSY;

	/* Set the MII address register to write */
	writel(addr, priv->ioaddr + mii_address);
	writel(value, priv->ioaddr + mii_data);

	/* Wait until any existing MII operation is complete */
	return readl_poll_timeout(priv->ioaddr + mii_data, tmp,
				  !(tmp & MII_XGMAC_BUSY), /*100*/10, 10000);
}
/**
 * __tc956xmac_xgmac2_mdio_write
 * @bus: points to the mii_bus structure
 * @phyaddr: MII addr
 * @phyreg: MII reg
 * @phydata: data to write into PHY reg
 * Description: Check whether MDIO bus is registered successfully or not
 * if registered then access MDIO for write operation
 */
static int tc956xmac_xgmac2_mdio_write(struct mii_bus *bus, int phyaddr,
				    int phyreg, u16 phydata)
{
	return bus->priv ?
		__tc956xmac_xgmac2_mdio_write(bus, phyaddr, phyreg, phydata) : -EIO;
}

#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
/**
 * tc956xmac_mdio_read
 * @bus: points to the mii_bus structure
 * @phyaddr: MII addr
 * @phyreg: MII reg
 * Description: it reads data from the MII register from within the phy device.
 * For the 7111 GMAC, we must set the bit 0 in the MII address register while
 * accessing the PHY registers.
 * Fortunately, it seems this has no drawback for the 7109 MAC.
 */
static int tc956xmac_mdio_read(struct mii_bus *bus, int phyaddr, int phyreg)
{
	struct net_device *ndev = bus->priv;
	struct tc956xmac_priv *priv = netdev_priv(ndev);
	unsigned int mii_address = priv->hw->mii.addr;
	unsigned int mii_data = priv->hw->mii.data;
	u32 value = MII_BUSY;
	int data = 0;
	u32 v;

	if ((priv->plat->c45_needed == true) && (phyreg < 0x1F)) {
		if (phyreg == 0)
			phyreg = (MII_ADDR_C45 | (PHY_CL45_CTRL_REG_MMD_BANK << 16) | PHY_CL45_CTRL_REG_ADDR);
		else if (phyreg == 1)
			phyreg = (MII_ADDR_C45 | (PHY_CL45_STATUS_REG_MMD_BANK << 16) | PHY_CL45_STATUS_REG_ADDR);
		else if (phyreg == 2)
			phyreg = (MII_ADDR_C45 | (PHY_CL45_PHYID1_MMD_BANK << 16) | PHY_CL45_PHYID1_ADDR);
		else if (phyreg == 3)
			phyreg = (MII_ADDR_C45 | (PHY_CL45_PHYID2_MMD_BANK << 16) | PHY_CL45_PHYID2_ADDR);
		else
			netdev_dbg(priv->dev, "%s Clause 45 register not defined for PHY register 0x%x\n", __func__, phyreg);

	}

	value |= (phyaddr << priv->hw->mii.addr_shift)
		& priv->hw->mii.addr_mask;
	value |= (phyreg << priv->hw->mii.reg_shift) & priv->hw->mii.reg_mask;
	value |= (priv->clk_csr << priv->hw->mii.clk_csr_shift)
		& priv->hw->mii.clk_csr_mask;
	if (priv->plat->has_gmac4) {
		value |= MII_GMAC4_READ;
		if (phyreg & MII_ADDR_C45) {
			value |= MII_GMAC4_C45E;
			value &= ~priv->hw->mii.reg_mask;
			value |= ((phyreg >> MII_DEVADDR_C45_SHIFT) <<
			       priv->hw->mii.reg_shift) &
			       priv->hw->mii.reg_mask;

			data |= (phyreg & MII_REGADDR_C45_MASK) <<
				MII_GMAC4_REG_ADDR_SHIFT;
		}
	}

	if (readl_poll_timeout(priv->ioaddr + mii_address, v, !(v & MII_BUSY),
			       100, 10000))
		return -EBUSY;

	writel(data, priv->ioaddr + mii_data);
	writel(value, priv->ioaddr + mii_address);

	if (readl_poll_timeout(priv->ioaddr + mii_address, v, !(v & MII_BUSY),
			       100, 10000))
		return -EBUSY;

	/* Read the data from the MII data register */
	data = (int)readl(priv->ioaddr + mii_data) & MII_DATA_MASK;

	return data;
}

/**
 * tc956xmac_mdio_write
 * @bus: points to the mii_bus structure
 * @phyaddr: MII addr
 * @phyreg: MII reg
 * @phydata: phy data
 * Description: it writes the data into the MII register from within the device.
 */
static int tc956xmac_mdio_write(struct mii_bus *bus, int phyaddr, int phyreg,
			     u16 phydata)
{
	struct net_device *ndev = bus->priv;
	struct tc956xmac_priv *priv = netdev_priv(ndev);
	unsigned int mii_address = priv->hw->mii.addr;
	unsigned int mii_data = priv->hw->mii.data;
	u32 value = MII_BUSY;
	int data = phydata;
	u32 v;

	value |= (phyaddr << priv->hw->mii.addr_shift)
		& priv->hw->mii.addr_mask;
	value |= (phyreg << priv->hw->mii.reg_shift) & priv->hw->mii.reg_mask;

	value |= (priv->clk_csr << priv->hw->mii.clk_csr_shift)
		& priv->hw->mii.clk_csr_mask;
	if (priv->plat->has_gmac4) {
		value |= MII_GMAC4_WRITE;
		if (phyreg & MII_ADDR_C45) {
			value |= MII_GMAC4_C45E;
			value &= ~priv->hw->mii.reg_mask;
			value |= ((phyreg >> MII_DEVADDR_C45_SHIFT) <<
			       priv->hw->mii.reg_shift) &
			       priv->hw->mii.reg_mask;

			data |= (phyreg & MII_REGADDR_C45_MASK) <<
				MII_GMAC4_REG_ADDR_SHIFT;
		}
	} else {
		value |= MII_WRITE;
	}

	/* Wait until any existing MII operation is complete */
	if (readl_poll_timeout(priv->ioaddr + mii_address, v, !(v & MII_BUSY),
			       100, 10000))
		return -EBUSY;

	/* Set the MII address register to write */
	writel(data, priv->ioaddr + mii_data);
	writel(value, priv->ioaddr + mii_address);

	/* Wait until any existing MII operation is complete */
	return readl_poll_timeout(priv->ioaddr + mii_address, v, !(v & MII_BUSY),
				  100, 10000);
}
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */

/**
 * tc956xmac_mdio_reset
 * @bus: points to the mii_bus structure
 * Description: reset the MII bus
 */
int tc956xmac_mdio_reset(struct mii_bus *bus)
{
#if IS_ENABLED(CONFIG_TC956XMAC_PLATFORM)
	struct net_device *ndev = bus->priv;
	struct tc956xmac_priv *priv = netdev_priv(ndev);
	unsigned int mii_address = priv->hw->mii.addr;

#ifdef CONFIG_OF
	if (priv->device->of_node) {
		struct gpio_desc *reset_gpio;
		u32 delays[3] = { 0, 0, 0 };

		reset_gpio = devm_gpiod_get_optional(priv->device,
						     "snps,reset",
						     GPIOD_OUT_LOW);
		if (IS_ERR(reset_gpio))
			return PTR_ERR(reset_gpio);

		device_property_read_u32_array(priv->device,
					       "snps,reset-delays-us",
					       delays, ARRAY_SIZE(delays));

		if (delays[0])
			msleep(DIV_ROUND_UP(delays[0], 1000));

		gpiod_set_value_cansleep(reset_gpio, 1);
		if (delays[1])
			msleep(DIV_ROUND_UP(delays[1], 1000));

		gpiod_set_value_cansleep(reset_gpio, 0);
		if (delays[2])
			msleep(DIV_ROUND_UP(delays[2], 1000));
	}
#endif

	/* This is a workaround for problems with the STE101P PHY.
	 * It doesn't complete its reset until at least one clock cycle
	 * on MDC, so perform a dummy mdio read. To be updated for GMAC4
	 * if needed.
	 */
	if (!priv->plat->has_gmac4)
		writel(0, priv->ioaddr + mii_address);
#endif
	return 0;
}

/**
 * tc956xmac_mdio_register
 * @ndev: net device structure
 * Description: it registers the MII bus
 */
int tc956xmac_mdio_register(struct net_device *ndev)
{
	int err = 0;
	struct mii_bus *new_bus;
	struct tc956xmac_priv *priv = netdev_priv(ndev);
	struct tc956xmac_mdio_bus_data *mdio_bus_data = priv->plat->mdio_bus_data;
	struct device_node *mdio_node = priv->plat->mdio_node;
	struct device *dev = ndev->dev.parent;
	int addr, found;

	if (!mdio_bus_data)
		return 0;

	new_bus = mdiobus_alloc();
	if (!new_bus)
		return -ENOMEM;

	if (mdio_bus_data->irqs)
		memcpy(new_bus->irq, mdio_bus_data->irqs, sizeof(new_bus->irq));

	new_bus->name = "tc956xmac";

	if (priv->plat->has_xgmac) {
		new_bus->read = &tc956xmac_xgmac2_mdio_read;
		new_bus->write = &tc956xmac_xgmac2_mdio_write;
#ifndef TC956X
		/* Check if DT specified an unsupported phy addr */
		if (priv->plat->phy_addr > MII_XGMAC_MAX_C22ADDR)
			dev_err(dev, "Unsupported phy_addr (max=%d)\n",
					MII_XGMAC_MAX_C22ADDR);
#endif
	}
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
	else {
		new_bus->read = &tc956xmac_mdio_read;
		new_bus->write = &tc956xmac_mdio_write;
	}
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */

	if (mdio_bus_data->needs_reset)
		new_bus->reset = &tc956xmac_mdio_reset;

	snprintf(new_bus->id, MII_BUS_ID_SIZE, "%s-%x",
		 new_bus->name, priv->plat->bus_id);
	new_bus->priv = ndev;
	new_bus->phy_mask = mdio_bus_data->phy_mask;
	new_bus->parent = priv->device;
#ifdef TC956X
	err = mdiobus_register(new_bus);
#else
	err = of_mdiobus_register(new_bus, mdio_node);
#endif
	if (err != 0) {
		dev_err(dev, "Cannot register the MDIO bus\n");
		goto bus_register_fail;
	}

	/* Looks like we need a dummy read for XGMAC only and C45 PHYs */
	if (priv->plat->has_xgmac)
		tc956xmac_xgmac2_mdio_read(new_bus, 0, MII_ADDR_C45);

#ifndef TC956X
	if (priv->plat->phy_node || mdio_node || priv->plat->has_xgmac)
		goto bus_register_done;
#endif
	found = 0;
	for (addr = 0; addr < PHY_MAX_ADDR; addr++) {

#ifdef TC956X
		int phy_reg_read;

		/* For C22 based PHYs, check for Status to detect PHY */
#ifdef TC956X
		phy_reg_read = tc956xmac_xgmac2_mdio_read(new_bus, addr, MII_BMSR);
#endif

		if (phy_reg_read != -EBUSY && phy_reg_read != -ENODEV) {
			if (phy_reg_read != 0x0000 && phy_reg_read != 0xffff) {
				if (priv->plat->c45_needed == true) 
					NMSGPR_ALERT(priv->device,
					    "TC956X: [1] Phy detected C45 at ID/ADDR %d\n", addr);
				else 
					NMSGPR_ALERT(priv->device,
					    "TC956X: [1] Phy detected C22 at ID/ADDR %d\n", addr);
#else
		struct phy_device *phydev = mdiobus_get_phy(new_bus, addr);

		if (!phydev)
			continue;
#endif
			/*
			 * If an IRQ was provided to be assigned after
			 * the bus probe, do it here.
			 */
			if (!mdio_bus_data->irqs &&
			    (mdio_bus_data->probed_phy_irq > 0)) {
				new_bus->irq[addr] = mdio_bus_data->probed_phy_irq;
#ifndef TC956X
				phydev->irq = mdio_bus_data->probed_phy_irq;
#endif
			}

			/*
			 * If we're going to bind the MAC to this PHY bus,
			 * and no PHY number was provided to the MAC,
			 * use the one probed here.
			 */
			if (priv->plat->phy_addr == -1)
				priv->plat->phy_addr = addr;
#ifndef TC956X
			phy_attached_info(phydev);
#endif
			found = 1;
			break;
#ifdef TC956X
			}
		} else {
			NMSGPR_ALERT(priv->device, "TC956X: Error reading the phy register"\
			    " MII_BMSR for phy ID/ADDR %d\n", addr);
		}
#endif
	}
	/* If C22 PHY is not found, probe for C45 based PHY*/
	if (!found) {
		for (addr = 0; addr < PHY_MAX_ADDR; addr++) {

#ifdef TC956X
			int phy_reg_read1, phy_reg_read2, phy_id;

			/* For C45 based PHYs, check for PHY ID to detect PHY */
			phy_reg_read1 = tc956xmac_xgmac2_mdio_read(new_bus, addr,
								((PHY_CL45_PHYID1_REG) | MII_ADDR_C45));
			phy_reg_read2 = tc956xmac_xgmac2_mdio_read(new_bus, addr,
								((PHY_CL45_PHYID2_REG) | MII_ADDR_C45));

			if (phy_reg_read1 != -EBUSY && phy_reg_read2 != -EBUSY) {
				phy_id = ((phy_reg_read1 << 16) | phy_reg_read2);
				if (phy_id != 0x00000000 && phy_id != 0xffffffff) {
					NMSGPR_ALERT(priv->device,
							"TC956X: [2] Phy detected C45 at ID/ADDR %d\n", addr);

#else
					struct phy_device *phydev = mdiobus_get_phy(new_bus, addr);

					if (!phydev)
						continue;
#endif
					/*
					 * If an IRQ was provided to be assigned after
					 * the bus probe, do it here.
					 */
					if (!mdio_bus_data->irqs &&
					    (mdio_bus_data->probed_phy_irq > 0)) {
						new_bus->irq[addr] = mdio_bus_data->probed_phy_irq;
#ifndef TC956X
						phydev->irq = mdio_bus_data->probed_phy_irq;
#endif
					}

					/*
					 * If we're going to bind the MAC to this PHY bus,
					 * and no PHY number was provided to the MAC,
					 * use the one probed here.
					 */
					if (priv->plat->phy_addr == -1)
						priv->plat->phy_addr = addr;

#ifndef TC956X
					phy_attached_info(phydev);
#endif
					found = 1;
					break;
#ifdef TC956X
				}
			} else {
				NMSGPR_ALERT(priv->device, "TC956X: Error reading the phy register"\
				    " MII_BMSR for phy ID/ADDR %d\n", addr);
			}
#endif
		}
	}

	if (!found && !mdio_node) {
		dev_warn(dev, "No PHY found\n");
		goto bus_no_phy_found;
	}
#ifndef TC956X
bus_register_done:
#endif
	priv->mii = new_bus;

	return 0;
bus_no_phy_found:
	err = -ENODEV;
	mdiobus_unregister(new_bus);
bus_register_fail:
	/* Set bus->priv to NULL, so that any future calls to bus read/write can avoid bus access.*/
	mutex_lock(&new_bus->mdio_lock);
	new_bus->priv = NULL;
	mutex_unlock(&new_bus->mdio_lock);

	mdiobus_free(new_bus);
	priv->mii = NULL;
	return err;
}

/**
 * tc956xmac_mdio_unregister
 * @ndev: net device structure
 * Description: it unregisters the MII bus
 */
int tc956xmac_mdio_unregister(struct net_device *ndev)
{
	struct tc956xmac_priv *priv = netdev_priv(ndev);

	if (!priv->mii)
		return 0;

	mdiobus_unregister(priv->mii);
	mutex_lock(&priv->mii->mdio_lock);
	priv->mii->priv = NULL;
	mutex_unlock(&priv->mii->mdio_lock);
	mdiobus_free(priv->mii);
	priv->mii = NULL;

	return 0;
}
