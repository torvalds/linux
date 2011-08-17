/*******************************************************************************

  Header file for stmmac platform data

  Copyright (C) 2009  STMicroelectronics Ltd

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#ifndef __STMMAC_PLATFORM_DATA
#define __STMMAC_PLATFORM_DATA

#include <linux/platform_device.h>

/* Platfrom data for platform device structure's platform_data field */

struct stmmac_mdio_bus_data {
	int bus_id;
	int (*phy_reset)(void *priv);
	unsigned int phy_mask;
	int *irqs;
	int probed_phy_irq;
};

struct plat_stmmacenet_data {
	int bus_id;
	int phy_addr;
	int interface;
	struct stmmac_mdio_bus_data *mdio_bus_data;
	int pbl;
	int clk_csr;
	int has_gmac;
	int enh_desc;
	int tx_coe;
	int bugged_jumbo;
	int pmt;
	int force_sf_dma_mode;
	void (*fix_mac_speed)(void *priv, unsigned int speed);
	void (*bus_setup)(void __iomem *ioaddr);
	int (*init)(struct platform_device *pdev);
	void (*exit)(struct platform_device *pdev);
	void *custom_cfg;
	void *bsp_priv;
};
#endif
