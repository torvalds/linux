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

#define STMMAC_RX_COE_NONE	0
#define STMMAC_RX_COE_TYPE1	1
#define STMMAC_RX_COE_TYPE2	2

/* Define the macros for CSR clock range parameters to be passed by
 * platform code.
 * This could also be configured at run time using CPU freq framework. */

/* MDC Clock Selection define*/
#define	STMMAC_CSR_60_100M	0	/* MDC = clk_scr_i/42 */
#define	STMMAC_CSR_100_150M	1	/* MDC = clk_scr_i/62 */
#define	STMMAC_CSR_20_35M	2	/* MDC = clk_scr_i/16 */
#define	STMMAC_CSR_35_60M	3	/* MDC = clk_scr_i/26 */
#define	STMMAC_CSR_150_250M	4	/* MDC = clk_scr_i/102 */
#define	STMMAC_CSR_250_300M	5	/* MDC = clk_scr_i/122 */

/* FIXME: The MDC clock could be set higher than the IEEE 802.3
 * specified frequency limit 0f 2.5 MHz, by programming a clock divider
 * of value different than the above defined values. The resultant MDIO
 * clock frequency of 12.5 MHz is applicable for the interfacing chips
 * supporting higher MDC clocks.
 * The MDC clock selection macros need to be defined for MDC clock rate
 * of 12.5 MHz, corresponding to the following selection.
 * 1000 clk_csr_i/4
 * 1001 clk_csr_i/6
 * 1010 clk_csr_i/8
 * 1011 clk_csr_i/10
 * 1100 clk_csr_i/12
 * 1101 clk_csr_i/14
 * 1110 clk_csr_i/16
 * 1111 clk_csr_i/18 */

/* Platfrom data for platform device structure's platform_data field */

struct stmmac_mdio_bus_data {
	int bus_id;
	int (*phy_reset)(void *priv);
	unsigned int phy_mask;
	int *irqs;
	int probed_phy_irq;
};

struct plat_stmmacenet_data {
	char *phy_bus_name;
	int bus_id;
	int phy_addr;
	int interface;
	struct stmmac_mdio_bus_data *mdio_bus_data;
	int pbl;
	int clk_csr;
	int has_gmac;
	int enh_desc;
	int tx_coe;
	int rx_coe;
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
