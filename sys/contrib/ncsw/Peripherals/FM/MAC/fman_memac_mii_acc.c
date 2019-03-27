/*
 * Copyright 2008-2013 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include "fsl_fman_memac_mii_acc.h"

static void write_phy_reg_10g(struct memac_mii_access_mem_map *mii_regs,
	uint8_t phy_addr, uint8_t reg, uint16_t data)
{
	uint32_t                tmp_reg;

	tmp_reg = ioread32be(&mii_regs->mdio_cfg);
	/* Leave only MDIO_CLK_DIV bits set on */
	tmp_reg &= MDIO_CFG_CLK_DIV_MASK;
	/* Set maximum MDIO_HOLD value to allow phy to see
	change of data signal */
	tmp_reg |= MDIO_CFG_HOLD_MASK;
	/* Add 10G interface mode */
	tmp_reg |= MDIO_CFG_ENC45;
	iowrite32be(tmp_reg, &mii_regs->mdio_cfg);

	/* Wait for command completion */
	while ((ioread32be(&mii_regs->mdio_cfg)) & MDIO_CFG_BSY)
		udelay(1);

	/* Specify phy and register to be accessed */
	iowrite32be(phy_addr, &mii_regs->mdio_ctrl);
	iowrite32be(reg, &mii_regs->mdio_addr);
	wmb();

	while ((ioread32be(&mii_regs->mdio_cfg)) & MDIO_CFG_BSY)
		udelay(1);

	/* Write data */
	iowrite32be(data, &mii_regs->mdio_data);
	wmb();

	/* Wait for write transaction end */
	while ((ioread32be(&mii_regs->mdio_data)) & MDIO_DATA_BSY)
		udelay(1);
}

static uint32_t read_phy_reg_10g(struct memac_mii_access_mem_map *mii_regs,
	uint8_t phy_addr, uint8_t reg, uint16_t *data)
{
	uint32_t                tmp_reg;

	tmp_reg = ioread32be(&mii_regs->mdio_cfg);
	/* Leave only MDIO_CLK_DIV bits set on */
	tmp_reg &= MDIO_CFG_CLK_DIV_MASK;
	/* Set maximum MDIO_HOLD value to allow phy to see
	change of data signal */
	tmp_reg |= MDIO_CFG_HOLD_MASK;
	/* Add 10G interface mode */
	tmp_reg |= MDIO_CFG_ENC45;
	iowrite32be(tmp_reg, &mii_regs->mdio_cfg);

	/* Wait for command completion */
	while ((ioread32be(&mii_regs->mdio_cfg)) & MDIO_CFG_BSY)
		udelay(1);

	/* Specify phy and register to be accessed */
	iowrite32be(phy_addr, &mii_regs->mdio_ctrl);
	iowrite32be(reg, &mii_regs->mdio_addr);
	wmb();

	while ((ioread32be(&mii_regs->mdio_cfg)) & MDIO_CFG_BSY)
		udelay(1);

	/* Read cycle */
	tmp_reg = phy_addr;
	tmp_reg |= MDIO_CTL_READ;
	iowrite32be(tmp_reg, &mii_regs->mdio_ctrl);
	wmb();

	/* Wait for data to be available */
	while ((ioread32be(&mii_regs->mdio_data)) & MDIO_DATA_BSY)
		udelay(1);

	*data =  (uint16_t)ioread32be(&mii_regs->mdio_data);

	/* Check if there was an error */
	return ioread32be(&mii_regs->mdio_cfg);
}

static void write_phy_reg_1g(struct memac_mii_access_mem_map *mii_regs,
	uint8_t phy_addr, uint8_t reg, uint16_t data)
{
	uint32_t                tmp_reg;

	/* Leave only MDIO_CLK_DIV and MDIO_HOLD bits set on */
	tmp_reg = ioread32be(&mii_regs->mdio_cfg);
	tmp_reg &= (MDIO_CFG_CLK_DIV_MASK | MDIO_CFG_HOLD_MASK);
	iowrite32be(tmp_reg, &mii_regs->mdio_cfg);

	/* Wait for command completion */
	while ((ioread32be(&mii_regs->mdio_cfg)) & MDIO_CFG_BSY)
		udelay(1);

	/* Write transaction */
	tmp_reg = (phy_addr << MDIO_CTL_PHY_ADDR_SHIFT);
	tmp_reg |= reg;
	iowrite32be(tmp_reg, &mii_regs->mdio_ctrl);

	while ((ioread32be(&mii_regs->mdio_cfg)) & MDIO_CFG_BSY)
		udelay(1);

	iowrite32be(data, &mii_regs->mdio_data);

	wmb();

	/* Wait for write transaction to end */
	while ((ioread32be(&mii_regs->mdio_data)) & MDIO_DATA_BSY)
		udelay(1);
}

static uint32_t read_phy_reg_1g(struct memac_mii_access_mem_map *mii_regs,
	uint8_t phy_addr, uint8_t reg, uint16_t *data)
{
	uint32_t tmp_reg;

	/* Leave only MDIO_CLK_DIV and MDIO_HOLD bits set on */
	tmp_reg = ioread32be(&mii_regs->mdio_cfg);
	tmp_reg &= (MDIO_CFG_CLK_DIV_MASK | MDIO_CFG_HOLD_MASK);
	iowrite32be(tmp_reg, &mii_regs->mdio_cfg);

	/* Wait for command completion */
	while ((ioread32be(&mii_regs->mdio_cfg)) & MDIO_CFG_BSY)
		udelay(1);

	/* Read transaction */
	tmp_reg = (phy_addr << MDIO_CTL_PHY_ADDR_SHIFT);
	tmp_reg |= reg;
	tmp_reg |= MDIO_CTL_READ;
	iowrite32be(tmp_reg, &mii_regs->mdio_ctrl);

	while ((ioread32be(&mii_regs->mdio_cfg)) & MDIO_CFG_BSY)
		udelay(1);

	/* Wait for data to be available */
	while ((ioread32be(&mii_regs->mdio_data)) & MDIO_DATA_BSY)
		udelay(1);

	*data =  (uint16_t)ioread32be(&mii_regs->mdio_data);

	/* Check error */
	return ioread32be(&mii_regs->mdio_cfg);
}

/*****************************************************************************/
int fman_memac_mii_write_phy_reg(struct memac_mii_access_mem_map *mii_regs,
	uint8_t phy_addr, uint8_t reg, uint16_t data,
	enum enet_speed enet_speed)
{
	/* Figure out interface type - 10G vs 1G.
	In 10G interface both phy_addr and devAddr present. */
	if (enet_speed == E_ENET_SPEED_10000)
		write_phy_reg_10g(mii_regs, phy_addr, reg, data);
	else
		write_phy_reg_1g(mii_regs, phy_addr, reg, data);

	return 0;
}

/*****************************************************************************/
int fman_memac_mii_read_phy_reg(struct memac_mii_access_mem_map *mii_regs,
	uint8_t phy_addr, uint8_t reg, uint16_t *data,
	enum enet_speed enet_speed)
{
	uint32_t ans;
	/* Figure out interface type - 10G vs 1G.
	In 10G interface both phy_addr and devAddr present. */
	if (enet_speed == E_ENET_SPEED_10000)
		ans = read_phy_reg_10g(mii_regs, phy_addr, reg, data);
	else
		ans = read_phy_reg_1g(mii_regs, phy_addr, reg, data);

	if (ans & MDIO_CFG_READ_ERR)
		return -EINVAL;
	return 0;
}

/* ......................................................................... */

