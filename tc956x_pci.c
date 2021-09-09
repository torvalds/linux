/*
 * TC956X ethernet driver.
 *
 * tc956x_pci.c
 *
 * Copyright (C) 2011-2012  Vayavya Labs Pvt Ltd
 * Copyright (C) 2021 Toshiba Electronic Devices & Storage Corporation
 *
 * This file has been derived from the STMicro and Synopsys Linux driver,
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
 *  15 Jul 2021 : 1. USXGMII/XFI/SGMII/RGMII interface supported without module parameter
 *  VERSION     : 01-00-02
 *  20 Jul 2021 : Version update
 *  VERSION     : 01-00-03
 *  22 Jul 2021 : 1. Version update
 *		: 2. USXGMII/XFI/SGMII/RGMII interface supported with module parameter
 *  VERSION     : 01-00-04
 *  22 Jul 2021 : 1. Dynamic CM3 TAMAP configuration
 *  VERSION     : 01-00-05
 *  23 Jul 2021 : 1. Add support for contiguous allocation of memory
 *  VERSION     : 01-00-06
 *  29 Jul 2021 : 1. Add support to set MAC Address register
 *  VERSION     : 01-00-07
 *  05 Aug 2021 : 1. Register Port0 as only PCIe device, incase its PHY is not found
 *  VERSION     : 01-00-08
 *  16 Aug 2021 : 1. PHY interrupt mode supported through .config_intr and .ack_interrupt API
 *  VERSION     : 01-00-09
 *  24 Aug 2021 : 1. Disable TC956X_PCIE_GEN3_SETTING and TC956X_LOAD_FW_HEADER macros and provide support via Makefile
 *		: 2. Platform API supported 
 *  VERSION     : 01-00-10
 *  02 Sep 2021 : 1. Configuration of Link state L0 and L1 transaction delay for PCIe switch ports & Endpoint.
 *  VERSION     : 01-00-11
 *  09 Sep 2021 : Reverted changes related to usage of Port-0 pci_dev for all DMA allocation/mapping for IPA path
 *  VERSION     : 01-00-12
 */

#include <linux/clk-provider.h>
#include <linux/pci.h>
#include <linux/dmi.h>
#include <linux/firmware.h>
#include <linux/version.h>
#include <linux/aer.h>
#include <linux/iopoll.h>
#include "dwxgmac2.h"
#include "tc956xmac.h"
#include "tc956xmac_config.h"
#include "tc956xmac_inc.h"
#include "common.h"

#ifdef TC956X_LOAD_FW_HEADER
#include "fw.h"
#endif

#ifdef TC956X_PCIE_LOGSTAT
#include "tc956x_pcie_logstat.h"
#endif /* #ifdef TC956X_PCIE_LOGSTAT */

#ifdef TC956X_PCIE_GEN3_SETTING
static unsigned int tc956x_speed = 3;
#endif
static unsigned int tc956x_port0_interface = ENABLE_XFI_INTERFACE;
static unsigned int tc956x_port1_interface = ENABLE_SGMII_INTERFACE;

static const struct tc956x_version tc956x_drv_version = {0, 1, 0, 0, 1, 2};

/*
 * This struct is used to associate PCI Function of MAC controller on a board,
 * discovered via DMI, with the address of PHY connected to the MAC. The
 * negative value of the address means that MAC controller is not connected
 * with PHY.
 */
struct tc956xmac_pci_func_data {
	unsigned int func;
	int phy_addr;
};

struct tc956xmac_pci_dmi_data {
	const struct tc956xmac_pci_func_data *func;
	size_t nfuncs;
};

struct tc956xmac_pci_info {
	int (*setup)(struct pci_dev *pdev, struct plat_tc956xmacenet_data *plat);
};

/*By default, route all packets to RxCh0 */

static struct tc956xmac_rx_parser_entry snps_rxp_entries[] = {
#ifdef TC956X
	{
		.match_data = 0x00000000, .match_en = 0x00000000, .af = 1, .rf = 0, .im = 0, .nc = 0, .res1 = 0, .frame_offset = 0, .res2 = 0, .ok_index = 0, .res3 = 0, .dma_ch_no = 1, .res4 = 0,
	},
#endif
};

#ifdef DMA_OFFLOAD_ENABLE
struct pci_dev* port0_pdev;
#endif

#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
static int tc956xmac_pci_find_phy_addr(struct pci_dev *pdev,
				const struct dmi_system_id *dmi_list)
{
	const struct tc956xmac_pci_func_data *func_data;
	const struct tc956xmac_pci_dmi_data *dmi_data;
	const struct dmi_system_id *dmi_id;
	int func = PCI_FUNC(pdev->devfn);
	size_t n;

	dmi_id = dmi_first_match(dmi_list);
	if (!dmi_id)
		return -ENODEV;

	dmi_data = dmi_id->driver_data;
	func_data = dmi_data->func;

	for (n = 0; n < dmi_data->nfuncs; n++, func_data++)
		if (func_data->func == func)
			return func_data->phy_addr;

	return -ENODEV;
}

static void common_default_data(struct plat_tc956xmacenet_data *plat)
{
	plat->clk_csr = 2;/* clk_csr_i = 20-35MHz & MDC = clk_csr_i/16 */
	plat->has_gmac = 1;

	plat->phy_addr = -1;
	plat->force_sf_dma_mode = 1;

	plat->mdio_bus_data->needs_reset = false;
	plat->mdio_bus_data->phy_mask = 0;

	/* Set default value for multicast hash bins */
	plat->multicast_filter_bins = HASH_TABLE_SIZE;

	/* Set the maxmtu to a default of JUMBO_LEN */
	plat->maxmtu = JUMBO_LEN;

	/* Set default number of RX and TX queues to use */
	plat->tx_queues_to_use = 1;
	plat->rx_queues_to_use = 1;

	/* Disable Priority config by default */
	plat->tx_queues_cfg[0].use_prio = false;
	plat->rx_queues_cfg[0].use_prio = false;

	/* Disable RX queues routing by default */
	plat->rx_queues_cfg[0].pkt_route = 0x0;
	/*For RXP config */
	plat->rxp_cfg.enable = false;

	plat->rxp_cfg.nve = ARRAY_SIZE(snps_rxp_entries);
	plat->rxp_cfg.npe = ARRAY_SIZE(snps_rxp_entries);
	memcpy(plat->rxp_cfg.entries, snps_rxp_entries,
			ARRAY_SIZE(snps_rxp_entries) *
			sizeof(struct tc956xmac_rx_parser_entry));

}

static int tc956xmac_default_data(struct pci_dev *pdev,
			       struct plat_tc956xmacenet_data *plat)
{
	KPRINT_INFO("%s  >", __func__);
	/* Set common default data first */
	common_default_data(plat);

	plat->dma_cfg->pbl = 32;
	plat->dma_cfg->pblx8 = true;
	/* TODO: AXI */

	plat->tx_dma_ch_owner[0] = TX_DMA_CH0_OWNER;
	plat->tx_dma_ch_owner[1] = TX_DMA_CH1_OWNER;
	plat->tx_dma_ch_owner[2] = TX_DMA_CH2_OWNER;
	plat->tx_dma_ch_owner[3] = TX_DMA_CH3_OWNER;
	plat->tx_dma_ch_owner[4] = TX_DMA_CH4_OWNER;
	plat->tx_dma_ch_owner[5] = TX_DMA_CH5_OWNER;
	plat->tx_dma_ch_owner[6] = TX_DMA_CH6_OWNER;
	plat->tx_dma_ch_owner[7] = TX_DMA_CH7_OWNER;

	plat->rx_dma_ch_owner[0] = RX_DMA_CH0_OWNER;
	plat->rx_dma_ch_owner[1] = RX_DMA_CH1_OWNER;
	plat->rx_dma_ch_owner[2] = RX_DMA_CH2_OWNER;
	plat->rx_dma_ch_owner[3] = RX_DMA_CH3_OWNER;
	plat->rx_dma_ch_owner[4] = RX_DMA_CH4_OWNER;
	plat->rx_dma_ch_owner[5] = RX_DMA_CH5_OWNER;
	plat->rx_dma_ch_owner[6] = RX_DMA_CH6_OWNER;
	plat->rx_dma_ch_owner[7] = RX_DMA_CH7_OWNER;

	KPRINT_INFO("%s  <", __func__);
	return 0;
}

static const struct tc956xmac_pci_info tc956xmac_pci_info = {
	.setup = tc956xmac_default_data,
};

static int intel_mgbe_common_data(struct pci_dev *pdev,
				struct plat_tc956xmacenet_data *plat)
{
	int i;

	plat->clk_csr = 5;
	plat->has_gmac = 0;
	plat->has_gmac4 = 1;
	plat->force_sf_dma_mode = 0;
	plat->tso_en = 1;

	plat->rx_sched_algorithm = MTL_RX_ALGORITHM_SP;

	for (i = 0; i < plat->rx_queues_to_use; i++) {
		plat->rx_queues_cfg[i].mode_to_use = MTL_QUEUE_DCB;
		plat->rx_queues_cfg[i].chan = i;

		/* Disable Priority config by default */
		plat->rx_queues_cfg[i].use_prio = false;

		/* Disable RX queues routing by default */
		plat->rx_queues_cfg[i].pkt_route = 0x0;
	}

	for (i = 0; i < plat->tx_queues_to_use; i++) {
		plat->tx_queues_cfg[i].mode_to_use = MTL_QUEUE_DCB;

		/* Disable Priority config by default */
		plat->tx_queues_cfg[i].use_prio = false;
	}

	/* FIFO size is 4096 bytes for 1 tx/rx queue */
	plat->tx_fifo_size = plat->tx_queues_to_use * 4096;
	plat->rx_fifo_size = plat->rx_queues_to_use * 4096;

	plat->tx_sched_algorithm = MTL_TX_ALGORITHM_WRR;
	plat->tx_queues_cfg[0].weight = 0x09;
	plat->tx_queues_cfg[1].weight = 0x0A;
	plat->tx_queues_cfg[2].weight = 0x0B;
	plat->tx_queues_cfg[3].weight = 0x0C;
	plat->tx_queues_cfg[4].weight = 0x0D;
	plat->tx_queues_cfg[5].weight = 0x0E;
	plat->tx_queues_cfg[6].weight = 0x0F;
	plat->tx_queues_cfg[7].weight = 0x10;

	plat->mdio_bus_data->phy_mask = 0;

	plat->dma_cfg->pbl = 32;
	plat->dma_cfg->pblx8 = true;
	plat->dma_cfg->fixed_burst = 0;
	plat->dma_cfg->mixed_burst = 0;
	plat->dma_cfg->aal = 0;

	plat->axi = devm_kzalloc(&pdev->dev, sizeof(*plat->axi),
				 GFP_KERNEL);
	if (!plat->axi)
		return -ENOMEM;

	plat->axi->axi_lpi_en = 0;
	plat->axi->axi_xit_frm = 0;
	plat->axi->axi_wr_osr_lmt = 1;
	plat->axi->axi_rd_osr_lmt = 1;
	plat->axi->axi_blen[0] = 4;
	plat->axi->axi_blen[1] = 8;
	plat->axi->axi_blen[2] = 16;

	plat->ptp_max_adj = plat->clk_ptp_rate;

	/* Set system clock */
	plat->tc956xmac_clk = clk_register_fixed_rate(&pdev->dev,
						"tc956xmac-clk", NULL, 0,
						plat->clk_ptp_rate);

	if (IS_ERR(plat->tc956xmac_clk)) {
		dev_warn(&pdev->dev, "Fail to register tc956xmac-clk\n");
		plat->tc956xmac_clk = NULL;
	}
	clk_prepare_enable(plat->tc956xmac_clk);

	/* Set default value for multicast hash bins */
	plat->multicast_filter_bins = HASH_TABLE_SIZE;

	/* Set default value for unicast filter entries */
	plat->unicast_filter_entries = 1;

	/* Set the maxmtu to a default of JUMBO_LEN */
	plat->maxmtu = JUMBO_LEN;

	return 0;
}

static int ehl_common_data(struct pci_dev *pdev,
			struct plat_tc956xmacenet_data *plat)
{
	int ret;

	plat->rx_queues_to_use = 8;
	plat->tx_queues_to_use = 8;
	plat->clk_ptp_rate = 200000000;
	ret = intel_mgbe_common_data(pdev, plat);
	if (ret)
		return ret;

	return 0;
}

static int ehl_sgmii_data(struct pci_dev *pdev,
			  struct plat_tc956xmacenet_data *plat)
{
	plat->bus_id = 1;
	plat->phy_addr = 0;
	plat->interface = PHY_INTERFACE_MODE_SGMII;
	return ehl_common_data(pdev, plat);
}

static struct tc956xmac_pci_info ehl_sgmii1g_pci_info = {
	.setup = ehl_sgmii_data,
};

static int ehl_rgmii_data(struct pci_dev *pdev,
			  struct plat_tc956xmacenet_data *plat)
{
	plat->bus_id = 1;
	plat->phy_addr = 0;
	plat->interface = PHY_INTERFACE_MODE_RGMII;
	return ehl_common_data(pdev, plat);
}

static struct tc956xmac_pci_info ehl_rgmii1g_pci_info = {
	.setup = ehl_rgmii_data,
};

static int tgl_common_data(struct pci_dev *pdev,
			   struct plat_tc956xmacenet_data *plat)
{
	int ret;

	plat->rx_queues_to_use = 6;
	plat->tx_queues_to_use = 4;
	plat->clk_ptp_rate = 200000000;
	ret = intel_mgbe_common_data(pdev, plat);
	if (ret)
		return ret;

	return 0;
}

static int tgl_sgmii_data(struct pci_dev *pdev,
			  struct plat_tc956xmacenet_data *plat)
{
	plat->bus_id = 1;
	plat->phy_addr = 0;
	plat->interface = PHY_INTERFACE_MODE_SGMII;

	return tgl_common_data(pdev, plat);
}

static struct tc956xmac_pci_info tgl_sgmii1g_pci_info = {
	.setup = tgl_sgmii_data,
};

static const struct tc956xmac_pci_func_data galileo_tc956xmac_func_data[] = {
	{
		.func = 6,
		.phy_addr = 1,
	},
};

static const struct tc956xmac_pci_dmi_data galileo_tc956xmac_dmi_data = {
	.func = galileo_tc956xmac_func_data,
	.nfuncs = ARRAY_SIZE(galileo_tc956xmac_func_data),
};

static const struct tc956xmac_pci_func_data iot2040_tc956xmac_func_data[] = {
	{
		.func = 6,
		.phy_addr = 1,
	},
	{
		.func = 7,
		.phy_addr = 1,
	},
};

static const struct tc956xmac_pci_dmi_data iot2040_tc956xmac_dmi_data = {
	.func = iot2040_tc956xmac_func_data,
	.nfuncs = ARRAY_SIZE(iot2040_tc956xmac_func_data),
};

static const struct dmi_system_id quark_pci_dmi[] = {
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "Galileo"),
		},
		.driver_data = (void *)&galileo_tc956xmac_dmi_data,
	},
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GalileoGen2"),
		},
		.driver_data = (void *)&galileo_tc956xmac_dmi_data,
	},
	/*
	 * There are 2 types of SIMATIC IOT2000: IOT2020 and IOT2040.
	 * The asset tag "6ES7647-0AA00-0YA2" is only for IOT2020 which
	 * has only one pci network device while other asset tags are
	 * for IOT2040 which has two.
	 */
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "SIMATIC IOT2000"),
			DMI_EXACT_MATCH(DMI_BOARD_ASSET_TAG,
					"6ES7647-0AA00-0YA2"),
		},
		.driver_data = (void *)&galileo_tc956xmac_dmi_data,
	},
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "SIMATIC IOT2000"),
		},
		.driver_data = (void *)&iot2040_tc956xmac_dmi_data,
	},
	{}
};

static int quark_default_data(struct pci_dev *pdev,
			      struct plat_tc956xmacenet_data *plat)
{
	int ret;

	/* Set common default data first */
	common_default_data(plat);

	/*
	 * Refuse to load the driver and register net device if MAC controller
	 * does not connect to any PHY interface.
	 */
	ret = tc956xmac_pci_find_phy_addr(pdev, quark_pci_dmi);
	if (ret < 0) {
		/* Return error to the caller on DMI enabled boards. */
		if (dmi_get_system_info(DMI_BOARD_NAME))
			return ret;

		/*
		 * Galileo boards with old firmware don't support DMI. We always
		 * use 1 here as PHY address, so at least the first found MAC
		 * controller would be probed.
		 */
		ret = 1;
	}

	plat->bus_id = pci_dev_id(pdev);
	plat->phy_addr = ret;
	plat->interface = PHY_INTERFACE_MODE_RMII;

	plat->dma_cfg->pbl = 16;
	plat->dma_cfg->pblx8 = true;
	plat->dma_cfg->fixed_burst = 1;
	/* AXI (TODO) */

	return 0;
}

static const struct tc956xmac_pci_info quark_pci_info = {
	.setup = quark_default_data,
};

static int snps_gmac5_default_data(struct pci_dev *pdev,
				   struct plat_tc956xmacenet_data *plat)
{
	int i;

	plat->clk_csr = 5;
	plat->has_gmac4 = 1;
	plat->force_sf_dma_mode = 1;
	plat->tso_en = 1;
	plat->sph_en = 1;
	plat->pmt = 1;

	plat->clk_ptp_rate = 62500000;
	plat->clk_ref_rate = 62500000;

	plat->mdio_bus_data->phy_mask = 0;

	/* Set default value for multicast hash bins */
	plat->multicast_filter_bins = HASH_TABLE_SIZE;

	/* Set default value for unicast filter entries */
	plat->unicast_filter_entries = 1;

	/* Set the maxmtu to a default of JUMBO_LEN */
	plat->maxmtu = JUMBO_LEN;

	/* Set default number of RX and TX queues to use */
	plat->tx_queues_to_use = 4;
	plat->rx_queues_to_use = 4;

	plat->tx_sched_algorithm = MTL_TX_ALGORITHM_WRR;
	for (i = 0; i < plat->tx_queues_to_use; i++) {
		plat->tx_queues_cfg[i].use_prio = false;
		plat->tx_queues_cfg[i].mode_to_use = MTL_QUEUE_DCB;
		plat->tx_queues_cfg[i].weight = 25;
		if (i > 0)
			plat->tx_queues_cfg[i].tbs_en = 1;
	}

	plat->rx_sched_algorithm = MTL_RX_ALGORITHM_SP;
	for (i = 0; i < plat->rx_queues_to_use; i++) {
		plat->rx_queues_cfg[i].use_prio = false;
		plat->rx_queues_cfg[i].mode_to_use = MTL_QUEUE_DCB;
		plat->rx_queues_cfg[i].pkt_route = 0x0;
		plat->rx_queues_cfg[i].chan = i;
	}

	plat->bus_id = 1;
	plat->phy_addr = -1;
	plat->interface = PHY_INTERFACE_MODE_GMII;

	plat->dma_cfg->pbl = 32;
	plat->dma_cfg->pblx8 = true;

	/* Axi Configuration */
	plat->axi = devm_kzalloc(&pdev->dev, sizeof(*plat->axi), GFP_KERNEL);
	if (!plat->axi)
		return -ENOMEM;

	plat->axi->axi_wr_osr_lmt = 31;
	plat->axi->axi_rd_osr_lmt = 31;

	plat->axi->axi_fb = false;
	plat->axi->axi_blen[0] = 4;
	plat->axi->axi_blen[1] = 8;
	plat->axi->axi_blen[2] = 16;
	plat->axi->axi_blen[3] = 32;

	/* EST Configuration */
	plat->est = devm_kzalloc(&pdev->dev, sizeof(*plat->est), GFP_KERNEL);
	if (!plat->est)
		return -ENOMEM;

	plat->est->enable = 0;
	plat->est->btr_offset[0] = 0;
	plat->est->btr_offset[1] = 0;
	plat->est->ctr[0] = 100000 * plat->tx_queues_to_use;
	plat->est->ctr[1] = 0;
	plat->est->ter = 0;
	plat->est->gcl_size = plat->tx_queues_to_use;

	for (i = 0; i < plat->tx_queues_to_use; i++) {
		u32 value = BIT(24 + i) + 100000;

		plat->est->gcl_unaligned[i] = value;
	}

	return 0;
}

static const struct tc956xmac_pci_info snps_gmac5_pci_info = {
	.setup = snps_gmac5_default_data,
};

#define XGMAC3_PHY_OFF		0x00008000
#define XGMAC3_PHY_ADDR		(XGMAC3_PHY_OFF + 0x00000ff0)

static int xgmac3_phy_read(void *priv, int phyaddr, int phyreg)
{
	struct tc956xmac_priv *spriv = priv;
	u32 off, ret;

	if (!(phyreg & MII_ADDR_C45))
		return -ENODEV;

	phyreg &= ~MII_ADDR_C45;
	off = (phyreg & GENMASK(7, 0)) << 4;

	writel(phyreg >> 8, spriv->ioaddr + XGMAC3_PHY_ADDR);
	readl(spriv->ioaddr + XGMAC3_PHY_ADDR);
	usleep_range(100, 200);
	readl(spriv->ioaddr + XGMAC3_PHY_OFF + off);
	usleep_range(100, 200);
	ret = readl(spriv->ioaddr + XGMAC3_PHY_OFF + off);

	return ret;
}

static int xgmac3_phy_write(void *priv, int phyaddr, int phyreg, u16 phydata)
{
	struct tc956xmac_priv *spriv = priv;
	u32 off;

	if (!(phyreg & MII_ADDR_C45))
		return -ENODEV;

	phyreg &= ~MII_ADDR_C45;
	off = (phyreg & GENMASK(7, 0)) << 4;

	writel(phyreg >> 8, spriv->ioaddr + XGMAC3_PHY_ADDR);
	readl(spriv->ioaddr + XGMAC3_PHY_ADDR);
	usleep_range(100, 200);
	writel(phydata, spriv->ioaddr + XGMAC3_PHY_OFF + off);
	readl(spriv->ioaddr + XGMAC3_PHY_OFF + off);
	usleep_range(100, 200);

	return 0;
}
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */

static void xgmac_default_data(struct plat_tc956xmacenet_data *plat)
{
	plat->has_xgmac = 1;
	plat->force_sf_dma_mode = 1;
	plat->tso_en = 1;
#ifdef TC956X
	plat->cphy_read = NULL;
	plat->cphy_write = NULL;
#else
	plat->cphy_read = xgmac3_phy_read;
	plat->cphy_write = xgmac3_phy_write;
#endif
	plat->mdio_bus_data->phy_mask = 0;
#ifdef TC956X

	switch (plat->mdc_clk) {
	case TC956XMAC_XGMAC_MDC_CSR_4:
		plat->clk_csr = 0x0;
		plat->clk_crs = 1;
		break;
	case TC956XMAC_XGMAC_MDC_CSR_6:
		plat->clk_csr = 0x1;
		plat->clk_crs = 1;
		break;
	case TC956XMAC_XGMAC_MDC_CSR_8:
		plat->clk_csr = 0x2;
		plat->clk_crs = 1;
		break;
	case TC956XMAC_XGMAC_MDC_CSR_10:
		plat->clk_csr = 0x3;
		plat->clk_crs = 1;
		break;
	case TC956XMAC_XGMAC_MDC_CSR_12:
		plat->clk_csr = 0x4;
		plat->clk_crs = 1;
		break;
	case TC956XMAC_XGMAC_MDC_CSR_14:
		plat->clk_csr = 0x5;
		plat->clk_crs = 1;
		break;
	case TC956XMAC_XGMAC_MDC_CSR_16:
		plat->clk_csr = 0x6;
		plat->clk_crs = 1;
		break;
	case TC956XMAC_XGMAC_MDC_CSR_18:
		plat->clk_csr = 0x7;
		plat->clk_crs = 1;
		break;
	case TC956XMAC_XGMAC_MDC_CSR_62:
		plat->clk_csr = 0x0;
		plat->clk_crs = 0;
		break;
	case TC956XMAC_XGMAC_MDC_CSR_102:
		plat->clk_csr = 0x1;
		plat->clk_crs = 0;
		break;
	case TC956XMAC_XGMAC_MDC_CSR_122:
		plat->clk_csr = 0x2;
		plat->clk_crs = 0;
		break;
	case TC956XMAC_XGMAC_MDC_CSR_142:
		plat->clk_csr = 0x3;
		plat->clk_crs = 0;
		break;
	case TC956XMAC_XGMAC_MDC_CSR_162:
		plat->clk_csr = 0x4;
		plat->clk_crs = 0;
		break;
	case TC956XMAC_XGMAC_MDC_CSR_202:
		plat->clk_csr = 0x5;
		plat->clk_crs = 0;
		break;

	};

	plat->has_gmac = 0;
	plat->has_gmac4 = 0;
	plat->force_thresh_dma_mode  = 0;
	plat->mdio_bus_data->needs_reset = false;
	if ((plat->port_interface == ENABLE_USXGMII_INTERFACE) ||
	   (plat->port_interface == ENABLE_XFI_INTERFACE))
		plat->mac_port_sel_speed = 10000;

	if (plat->port_interface == ENABLE_RGMII_INTERFACE)
		plat->mac_port_sel_speed = 1000;

	if (plat->port_interface == ENABLE_SGMII_INTERFACE) {
		plat->mac_port_sel_speed = 2500;
	}

	plat->riwt_off = 0;
	plat->rss_en = 0;
#endif

	/*For RXP config */
#ifdef TC956X_FRP_ENABLE
	plat->rxp_cfg.enable = true;
#else
	plat->rxp_cfg.enable = false;
#endif

	plat->rxp_cfg.nve = ARRAY_SIZE(snps_rxp_entries);
	plat->rxp_cfg.npe = ARRAY_SIZE(snps_rxp_entries);
	memcpy(plat->rxp_cfg.entries, snps_rxp_entries,
			ARRAY_SIZE(snps_rxp_entries) *
			sizeof(struct tc956xmac_rx_parser_entry));

}

static int tc956xmac_xgmac3_default_data(struct pci_dev *pdev,
				struct plat_tc956xmacenet_data *plat)
{
	/* Set common default data first */
	xgmac_default_data(plat);

	plat->bus_id = 1;
#ifdef TC956X
	plat->phy_addr = -1;
#else
	plat->phy_addr = 0;
#endif
	plat->pdev = pdev;

#ifdef TC956X
	if (plat->port_interface == ENABLE_USXGMII_INTERFACE) {
		plat->interface = PHY_INTERFACE_MODE_USXGMII;
		plat->max_speed = 10000;
	}
	if (plat->port_interface == ENABLE_XFI_INTERFACE) {
		plat->interface = PHY_INTERFACE_MODE_10GKR;
		plat->max_speed = 10000;
	}
	if (plat->port_interface == ENABLE_RGMII_INTERFACE) {
		plat->interface = PHY_INTERFACE_MODE_RGMII;
		plat->max_speed = 1000;
	}
	if (plat->port_interface == ENABLE_SGMII_INTERFACE) {
		plat->interface = PHY_INTERFACE_MODE_SGMII;
		plat->max_speed = 2500;
	}
#else
	plat->interface = PHY_INTERFACE_MODE_USXGMII;
	plat->max_speed = 10000;

#endif
	plat->phy_interface = plat->interface;

#ifdef TC956X
	plat->clk_ptp_rate = TC956X_TARGET_PTP_CLK;
#else
	plat->clk_ref_rate = 62500000;
#endif

#ifdef TC956X
	/* Set default value for multicast hash bins */
	plat->multicast_filter_bins = HASH_TABLE_SIZE;
	/* Set default value for unicast filter entries */
	plat->unicast_filter_entries = MAX_MAC_ADDR_FILTERS;
#else
	plat->multicast_filter_bins = 0;
	/* Set default value for unicast filter entries */
	plat->unicast_filter_entries = 1;

#endif

	plat->maxmtu = XGMAC_JUMBO_LEN;

	/* Set default number of RX and TX queues to use */
#ifdef TC956X
	plat->tx_queues_to_use = MAX_TX_QUEUES_TO_USE;
	plat->rx_queues_to_use = MAX_RX_QUEUES_TO_USE;
#else
	plat->tx_queues_to_use = 1;
	plat->rx_queues_to_use = 1;
#endif

#ifdef TC956X
	/* MTL Configuration */
	/* Static Mapping */
	/* Unicast/Untagged Packets : Consider Jumbo packets */
	plat->rx_queues_cfg[0].chan = LEG_UNTAGGED_PACKET;
	/* VLAN Tagged Legacy packets */
	plat->rx_queues_cfg[1].chan = LEG_TAGGED_PACKET;
	/* Untagged gPTP packets  */
	plat->rx_queues_cfg[2].chan = UNTAGGED_GPTP_PACKET;
	/* Tagged/Untagged  AV control pkts */
	plat->rx_queues_cfg[3].chan = UNTAGGED_AVCTRL_PACKET;
	/* AVB Class B */
	plat->rx_queues_cfg[4].chan = AVB_CLASS_B_PACKET;
	/* AVB Class A */
	plat->rx_queues_cfg[5].chan = AVB_CLASS_A_PACKET;
	/* CDT */
	plat->rx_queues_cfg[6].chan = TSN_CLASS_CDT_PACKET;
	/* Broadcast/Multicast packets to support pkt duplication it should be highest queue */
	plat->rx_queues_cfg[7].chan = BC_MC_PACKET;

	/* Rx Queue Packet routing */
	plat->rx_queues_cfg[0].pkt_route = RX_QUEUE0_PKT_ROUTE;
	plat->rx_queues_cfg[1].pkt_route = RX_QUEUE1_PKT_ROUTE;
	plat->rx_queues_cfg[2].pkt_route = RX_QUEUE2_PKT_ROUTE;
	plat->rx_queues_cfg[3].pkt_route = RX_QUEUE3_PKT_ROUTE;
	plat->rx_queues_cfg[4].pkt_route = RX_QUEUE4_PKT_ROUTE;
	plat->rx_queues_cfg[5].pkt_route = RX_QUEUE5_PKT_ROUTE;
	plat->rx_queues_cfg[6].pkt_route = RX_QUEUE6_PKT_ROUTE;
	plat->rx_queues_cfg[7].pkt_route = RX_QUEUE7_PKT_ROUTE;
	/* MTL Scheduler for RX and TX */

	plat->rx_sched_algorithm = MTL_RX_ALGORITHM_SP;
	plat->tx_sched_algorithm = MTL_TX_ALGORITHM_WRR;

	/* Due to the erratum in XGMAC 3.01a, WRR weights not considered in
	 * TX DMA read data arbitration. Workaround is at set all weights for Tx Queues with
	 * WRR arbitration logic to 1
	 */
	plat->tx_queues_cfg[0].weight = 1;
	plat->tx_queues_cfg[1].weight = 1;
	plat->tx_queues_cfg[2].weight = 1;
	plat->tx_queues_cfg[3].weight = 1;
	plat->tx_queues_cfg[4].weight = 1;
	plat->tx_queues_cfg[5].weight = 1;
	plat->tx_queues_cfg[6].weight = 1;
	plat->tx_queues_cfg[7].weight = 1;

	plat->rx_queues_cfg[0].mode_to_use = RX_QUEUE0_MODE;
	plat->rx_queues_cfg[1].mode_to_use = RX_QUEUE1_MODE;
	plat->rx_queues_cfg[2].mode_to_use = RX_QUEUE2_MODE;
	plat->rx_queues_cfg[3].mode_to_use = RX_QUEUE3_MODE;
	plat->rx_queues_cfg[4].mode_to_use = RX_QUEUE4_MODE;
	plat->rx_queues_cfg[5].mode_to_use = RX_QUEUE5_MODE;
	plat->rx_queues_cfg[6].mode_to_use = RX_QUEUE6_MODE;
	plat->rx_queues_cfg[7].mode_to_use = RX_QUEUE7_MODE;

	plat->tx_queues_cfg[0].mode_to_use = TX_QUEUE0_MODE;
	plat->tx_queues_cfg[1].mode_to_use = TX_QUEUE1_MODE;
	plat->tx_queues_cfg[2].mode_to_use = TX_QUEUE2_MODE;
	plat->tx_queues_cfg[3].mode_to_use = TX_QUEUE3_MODE;
	plat->tx_queues_cfg[4].mode_to_use = TX_QUEUE4_MODE;
	plat->tx_queues_cfg[5].mode_to_use = TX_QUEUE5_MODE;
	plat->tx_queues_cfg[6].mode_to_use = TX_QUEUE6_MODE;
	plat->tx_queues_cfg[7].mode_to_use = TX_QUEUE7_MODE;

	/* CBS: queue 5 -> Class B traffic (25% BW) */
	/* plat->tx_queues_cfg[3].idle_slope = plat->est_cfg.enable ? 0x8e4 : 0x800;
	*/
	plat->tx_queues_cfg[5].idle_slope = 0x800;
	plat->tx_queues_cfg[5].send_slope = 0x1800;
	plat->tx_queues_cfg[5].high_credit = 0x320000;
	plat->tx_queues_cfg[5].low_credit = 0xff6a0000;

	/* CBS: queue 6 -> Class A traffic (25% BW) */
	/* plat->tx_queues_cfg[5].idle_slope = plat->est_cfg.enable ? 0x8e4 : 0x800; */
	plat->tx_queues_cfg[6].idle_slope = 0x800;
	plat->tx_queues_cfg[6].send_slope = 0x1800;
	plat->tx_queues_cfg[6].high_credit = 0x320000;
	plat->tx_queues_cfg[6].low_credit = 0xff6a0000;

	/* CBS: queue 7 -> Class CDT traffic (40%) BW */
	/* plat->tx_queues_cfg[4].idle_slope = plat->est_cfg.enable ? 0xe38 : 0xccc; */
	plat->tx_queues_cfg[7].idle_slope = 0xccc;
	plat->tx_queues_cfg[7].send_slope = 0x1333;
	plat->tx_queues_cfg[7].high_credit = 0x500000;
	plat->tx_queues_cfg[7].low_credit = 0xff880000;

	/* Disable Priority config by default */
	plat->tx_queues_cfg[0].use_prio = false;
	plat->tx_queues_cfg[1].use_prio = false;
	plat->tx_queues_cfg[2].use_prio = false;
	plat->tx_queues_cfg[3].use_prio = false;
	plat->tx_queues_cfg[4].use_prio = false;
	plat->tx_queues_cfg[5].use_prio = false;
	plat->tx_queues_cfg[6].use_prio = false;
	plat->tx_queues_cfg[7].use_prio = false;

	/* Enable/Disable TBS */
	plat->tx_queues_cfg[0].tbs_en = TX_QUEUE0_TBS;
	plat->tx_queues_cfg[1].tbs_en = TX_QUEUE1_TBS;
	plat->tx_queues_cfg[2].tbs_en = TX_QUEUE2_TBS;
	plat->tx_queues_cfg[3].tbs_en = TX_QUEUE3_TBS;
	plat->tx_queues_cfg[4].tbs_en = TX_QUEUE4_TBS;
	plat->tx_queues_cfg[5].tbs_en = TX_QUEUE5_TBS;
	plat->tx_queues_cfg[6].tbs_en = TX_QUEUE6_TBS;
	plat->tx_queues_cfg[7].tbs_en = TX_QUEUE7_TBS;

	/* Enable/Disable TSO*/
	plat->tx_queues_cfg[0].tso_en = TX_QUEUE0_TSO;
	plat->tx_queues_cfg[1].tso_en = TX_QUEUE1_TSO;
	plat->tx_queues_cfg[2].tso_en = TX_QUEUE2_TSO;
	plat->tx_queues_cfg[3].tso_en = TX_QUEUE3_TSO;
	plat->tx_queues_cfg[4].tso_en = TX_QUEUE4_TSO;
	plat->tx_queues_cfg[5].tso_en = TX_QUEUE5_TSO;
	plat->tx_queues_cfg[6].tso_en = TX_QUEUE6_TSO;
	plat->tx_queues_cfg[7].tso_en = TX_QUEUE7_TSO;

	plat->tx_queues_cfg[0].traffic_class = TX_QUEUE0_TC;
	plat->tx_queues_cfg[1].traffic_class = TX_QUEUE1_TC;
	plat->tx_queues_cfg[2].traffic_class = TX_QUEUE2_TC;
	plat->tx_queues_cfg[3].traffic_class = TX_QUEUE3_TC;
	plat->tx_queues_cfg[4].traffic_class = TX_QUEUE4_TC;
	plat->tx_queues_cfg[5].traffic_class = TX_QUEUE5_TC;
	plat->tx_queues_cfg[6].traffic_class = TX_QUEUE6_TC;
	plat->tx_queues_cfg[7].traffic_class = TX_QUEUE7_TC;


	plat->rx_queues_cfg[0].use_prio = RX_QUEUE0_USE_PRIO;
	plat->rx_queues_cfg[0].prio = RX_QUEUE0_PRIO;

	plat->rx_queues_cfg[1].use_prio = RX_QUEUE1_USE_PRIO;
	plat->rx_queues_cfg[1].prio = RX_QUEUE1_PRIO;

	plat->rx_queues_cfg[2].use_prio = RX_QUEUE2_USE_PRIO;
	plat->rx_queues_cfg[2].prio = RX_QUEUE2_PRIO;

	plat->rx_queues_cfg[3].use_prio = RX_QUEUE3_USE_PRIO;
	plat->rx_queues_cfg[3].prio = RX_QUEUE3_PRIO;

	plat->rx_queues_cfg[4].use_prio = RX_QUEUE4_USE_PRIO;
	plat->rx_queues_cfg[4].prio = RX_QUEUE4_PRIO;

	plat->rx_queues_cfg[5].use_prio = RX_QUEUE5_USE_PRIO;
	plat->rx_queues_cfg[5].prio = RX_QUEUE5_PRIO;

	plat->rx_queues_cfg[6].use_prio = RX_QUEUE6_USE_PRIO;
	plat->rx_queues_cfg[6].prio = RX_QUEUE6_PRIO;

	plat->rx_queues_cfg[7].use_prio = RX_QUEUE7_USE_PRIO;
	plat->rx_queues_cfg[7].prio = RX_QUEUE7_PRIO;

#else
	/* Disable Priority config by default */
	plat->tx_queues_cfg[0].use_prio = false;
	plat->tx_queues_cfg[0].mode_to_use = MTL_QUEUE_DCB;
	plat->rx_queues_cfg[0].use_prio = false;
	plat->rx_queues_cfg[0].mode_to_use = MTL_QUEUE_DCB;

	/* Disable RX queues routing by default */
	plat->rx_queues_cfg[0].pkt_route = 0x0;
#endif

#ifdef TC956X
	plat->dma_cfg->txpbl = 16;
	plat->dma_cfg->rxpbl = 16;
	plat->dma_cfg->pblx8 = true;
#else
	plat->dma_cfg->pbl = 32;
	plat->dma_cfg->pblx8 = true;
#endif
	/* Axi Configuration */
	plat->axi = devm_kzalloc(&pdev->dev, sizeof(*plat->axi), GFP_KERNEL);
	if (!plat->axi)
		return -ENOMEM;

#ifdef TC956X
	plat->axi->axi_lpi_en = 0;
	plat->axi->axi_xit_frm = 0;
	plat->axi->axi_wr_osr_lmt = 31;
	plat->axi->axi_rd_osr_lmt = 31;
#else
	plat->axi->axi_wr_osr_lmt = 31;
	plat->axi->axi_rd_osr_lmt = 31;
#endif
	plat->axi->axi_fb = false;
	plat->axi->axi_blen[0] = 4;
	plat->axi->axi_blen[1] = 8;
	plat->axi->axi_blen[2] = 16;
	plat->axi->axi_blen[3] = 32;
	plat->axi->axi_blen[4] = 64;
	plat->axi->axi_blen[5] = 128;
	plat->axi->axi_blen[6] = 256;

	if (!plat->est) {
		plat->est = devm_kzalloc(&pdev->dev, sizeof(*plat->est),
					 GFP_KERNEL);
		if (!plat->est)
			return -ENOMEM;
	} else {
		memset(plat->est, 0, sizeof(*plat->est));
	}

	plat->tx_dma_ch_owner[0] = TX_DMA_CH0_OWNER;
	plat->tx_dma_ch_owner[1] = TX_DMA_CH1_OWNER;
	plat->tx_dma_ch_owner[2] = TX_DMA_CH2_OWNER;
	plat->tx_dma_ch_owner[3] = TX_DMA_CH3_OWNER;
	plat->tx_dma_ch_owner[4] = TX_DMA_CH4_OWNER;
	plat->tx_dma_ch_owner[5] = TX_DMA_CH5_OWNER;
	plat->tx_dma_ch_owner[6] = TX_DMA_CH6_OWNER;
	plat->tx_dma_ch_owner[7] = TX_DMA_CH7_OWNER;

	plat->rx_dma_ch_owner[0] = RX_DMA_CH0_OWNER;
	plat->rx_dma_ch_owner[1] = RX_DMA_CH1_OWNER;
	plat->rx_dma_ch_owner[2] = RX_DMA_CH2_OWNER;
	plat->rx_dma_ch_owner[3] = RX_DMA_CH3_OWNER;
	plat->rx_dma_ch_owner[4] = RX_DMA_CH4_OWNER;
	plat->rx_dma_ch_owner[5] = RX_DMA_CH5_OWNER;
	plat->rx_dma_ch_owner[6] = RX_DMA_CH6_OWNER;
	plat->rx_dma_ch_owner[7] = RX_DMA_CH7_OWNER;

	/* Configuration of PHY operating mode 1(true): for interrupt mode, 0(false): for polling mode */
	if (plat->port_num == RM_PF0_ID) {
#ifdef TC956X_PHY_INTERRUPT_MODE_EMAC0
		plat->phy_interrupt_mode = true;
#else
		plat->phy_interrupt_mode = false;
#endif
	}

	if (plat->port_num == RM_PF1_ID) {
#ifdef TC956X_PHY_INTERRUPT_MODE_EMAC1
		plat->phy_interrupt_mode = true;
#else
		plat->phy_interrupt_mode = false;
#endif
	}
	return 0;
}

static const struct tc956xmac_pci_info tc956xmac_xgmac3_pci_info = {
	.setup = tc956xmac_xgmac3_default_data,
};

#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
static void xgmac_2_5g_default_data(struct plat_tc956xmacenet_data *plat)
{
	plat->clk_csr = 2;
	plat->has_xgmac = 1;
	plat->force_sf_dma_mode = 1;
	plat->tso_en = 1;
	plat->sph_en = 1;
	plat->rss_en = 1;

	plat->cphy_read = xgmac3_phy_read;
	plat->cphy_write = xgmac3_phy_write;
	plat->mdio_bus_data->phy_mask = 0;

	plat->clk_ptp_rate = 62500000;
	plat->clk_ref_rate = 62500000;

	/* Set default value for multicast hash bins */
	plat->multicast_filter_bins = 128;

	/* Set default value for unicast filter entries */
	plat->unicast_filter_entries = 32;

	/* Set the maxmtu to a default of JUMBO_LEN */
	plat->maxmtu = XGMAC_JUMBO_LEN;

	/* Set default number of RX and TX queues to use */
	plat->tx_queues_to_use = 4;
	plat->rx_queues_to_use = 8;

	plat->tx_sched_algorithm = MTL_TX_ALGORITHM_WRR;
	plat->tx_queues_cfg[0].use_prio = false;
	plat->tx_queues_cfg[0].mode_to_use = MTL_QUEUE_DCB;
	plat->tx_queues_cfg[0].weight = 12;
	plat->tx_queues_cfg[1].use_prio = false;
	plat->tx_queues_cfg[1].mode_to_use = MTL_QUEUE_DCB;
	plat->tx_queues_cfg[1].weight = 12;
	plat->tx_queues_cfg[2].use_prio = false;
	plat->tx_queues_cfg[2].mode_to_use = MTL_QUEUE_DCB;
	plat->tx_queues_cfg[2].weight = 12;
	plat->tx_queues_cfg[3].use_prio = false;
	plat->tx_queues_cfg[3].mode_to_use = MTL_QUEUE_DCB;
	plat->tx_queues_cfg[3].weight = 12;
	plat->tx_queues_cfg[4].use_prio = false;
	plat->tx_queues_cfg[4].mode_to_use = MTL_QUEUE_DCB;
	plat->tx_queues_cfg[4].weight = 12;
	plat->tx_queues_cfg[5].use_prio = false;
	plat->tx_queues_cfg[5].mode_to_use = MTL_QUEUE_DCB;
	plat->tx_queues_cfg[5].weight = 12;
	plat->tx_queues_cfg[6].use_prio = false;
	plat->tx_queues_cfg[6].mode_to_use = MTL_QUEUE_DCB;
	plat->tx_queues_cfg[6].weight = 12;
	plat->tx_queues_cfg[7].use_prio = false;
	plat->tx_queues_cfg[7].mode_to_use = MTL_QUEUE_DCB;
	plat->tx_queues_cfg[7].weight = 12;
	plat->rx_queues_cfg[0].use_prio = false;
	plat->rx_queues_cfg[0].mode_to_use = MTL_QUEUE_DCB;
	plat->rx_queues_cfg[0].chan = 0;
	plat->rx_queues_cfg[1].use_prio = false;
	plat->rx_queues_cfg[1].mode_to_use = MTL_QUEUE_DCB;
	plat->rx_queues_cfg[1].chan = 1;
	plat->rx_queues_cfg[2].use_prio = false;
	plat->rx_queues_cfg[2].mode_to_use = MTL_QUEUE_DCB;
	plat->rx_queues_cfg[2].chan = 2;
	plat->rx_queues_cfg[3].use_prio = false;
	plat->rx_queues_cfg[3].mode_to_use = MTL_QUEUE_DCB;
	plat->rx_queues_cfg[3].chan = 3;
	plat->rx_queues_cfg[4].use_prio = false;
	plat->rx_queues_cfg[4].mode_to_use = MTL_QUEUE_DCB;
	plat->rx_queues_cfg[4].chan = 4;
	plat->rx_queues_cfg[5].use_prio = false;
	plat->rx_queues_cfg[5].mode_to_use = MTL_QUEUE_DCB;
	plat->rx_queues_cfg[5].chan = 5;
	plat->rx_queues_cfg[6].use_prio = false;
	plat->rx_queues_cfg[6].mode_to_use = MTL_QUEUE_DCB;
	plat->rx_queues_cfg[6].chan = 6;
	plat->rx_queues_cfg[7].use_prio = false;
	plat->rx_queues_cfg[7].mode_to_use = MTL_QUEUE_DCB;
	plat->rx_queues_cfg[7].chan = 7;
}

static int tc956xmac_xgmac3_2_5g_default_data(struct pci_dev *pdev,
					struct plat_tc956xmacenet_data *plat)
{
	int i;

	/* Set common default data first */
	xgmac_2_5g_default_data(plat);

	plat->bus_id = 1;
	plat->phy_addr = 0;
	plat->interface = PHY_INTERFACE_MODE_USXGMII;
	plat->max_speed = 2500;

	plat->dma_cfg->pbl = 32;
	plat->dma_cfg->pblx8 = true;

	/* Axi Configuration */
	plat->axi = devm_kzalloc(&pdev->dev, sizeof(*plat->axi), GFP_KERNEL);
	if (!plat->axi)
		return -ENOMEM;

	plat->axi->axi_wr_osr_lmt = 31;
	plat->axi->axi_rd_osr_lmt = 31;

	plat->axi->axi_fb = false;
	plat->axi->axi_blen[0] = 4;
	plat->axi->axi_blen[1] = 8;
	plat->axi->axi_blen[2] = 16;
	plat->axi->axi_blen[3] = 32;

	/* EST Configuration */
	plat->est = devm_kzalloc(&pdev->dev, sizeof(*plat->est), GFP_KERNEL);
	if (!plat->est)
		return -ENOMEM;

	plat->est->enable = 0;
	plat->est->btr_offset[0] = 0;
	plat->est->btr_offset[1] = 0;
	plat->est->ctr[0] = 100000 * plat->tx_queues_to_use;
	plat->est->ctr[1] = 0;
	plat->est->ter = 0;
	plat->est->gcl_size = plat->tx_queues_to_use;

	for (i = 0; i < plat->tx_queues_to_use; i++) {
		u32 value = BIT(24 + i) + 100000;

		plat->est->gcl_unaligned[i] = value;
	}

	tc956xmac_config_data(plat);
	return 0;
}

static const struct tc956xmac_pci_info tc956xmac_xgmac3_2_5g_pci_info = {
	.setup = tc956xmac_xgmac3_2_5g_default_data,
};

static int tc956xmac_xgmac3_2_5g_mdio_default_data(struct pci_dev *pdev,
						struct plat_tc956xmacenet_data *plat)
{
	int ret;

	ret = tc956xmac_xgmac3_2_5g_default_data(pdev, plat);
	if (ret)
		return ret;

	plat->mdio_bus_data->phy_mask = ~0x0;
	plat->bus_id = 1;
	plat->phy_addr = 0;

	plat->cphy_read = NULL;
	plat->cphy_write = NULL;
	return 0;
}

static const struct tc956xmac_pci_info tc956xmac_xgmac3_2_5g_mdio_pci_info = {
	.setup = tc956xmac_xgmac3_2_5g_mdio_default_data,
};
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */

/*!
 * \brief API to load firmware for CM3.
 *
 * \details This fucntion loads the firmware onto the SRAM of tc956x.
 * The tc956x CM3 starts executing once the firmware loading is complete.
 *
 * \param[in] dev  - pointer to device structure.
 * \param[in] id   - pointer to tc956xmac_resources structure.
 *
 * \return integer
 *
 * \retval 0 on success & -ve number on failure.
 */

s32 tc956x_load_firmware(struct device *dev, struct tc956xmac_resources *res)
{
	u32 adrs = 0, val = 0;
	u32 fw_init_sync;
#ifdef TC956X_LOAD_FW_HEADER
	u32 fw_size = sizeof(fw_data);

	NMSGPR_INFO(dev,  "FW Loading: .h\n");
	/* Validate the size of the firmware */
	if (fw_size > TC956X_FW_MAX_SIZE) {
		NMSGPR_ERR(dev, "Error : FW size exceeds the memory size\n");
		return -EINVAL;
	}

	NMSGPR_INFO(dev,  "FW Loading Start...\n");
	NMSGPR_INFO(dev,  "FW Size = %d\n", fw_size);

#ifdef TC956X
	/* Assert M3 reset */
	adrs = NRSTCTRL0_OFFSET;
#else
	adrs = NRSTCTRL_OFFSET;
#endif
	val = ioread32((void __iomem *)(res->addr + adrs));
	NMSGPR_INFO(dev,  "Reset Register value = %lx\n", (unsigned long)val);

	val |= NRSTCTRL0_RST_ASRT;
	iowrite32(val, (void __iomem *)(res->addr + adrs));

#ifdef TC956X
	iowrite32(0, (void __iomem *)(res->tc956x_SRAM_pci_base_addr +
			TC956X_M3_INIT_DONE));
	iowrite32(0, (void __iomem *)(res->tc956x_SRAM_pci_base_addr +
			TC956X_M3_FW_EXIT));
#endif
	/* Copy TC956X FW to SRAM */
	adrs = TC956X_ZERO;/* SRAM Start Address */
	do {
		val =  fw_data[adrs + TC956X_ZERO] << TC956X_ZERO;
		val |= fw_data[adrs + TC956X_ONE] << TC956X_EIGHT;
		val |= fw_data[adrs + TC956X_TWO] << TC956X_SIXTEEN;
		val |= fw_data[adrs + TC956X_THREE] << TC956X_TWENTY_FOUR;

#ifdef TC956X
		iowrite32(val, (void __iomem *)(res->tc956x_SRAM_pci_base_addr +
			adrs));
#endif

		adrs += TC956X_FOUR;
	} while (adrs < fw_size);

#else
	const struct firmware *pfw = NULL;

	NMSGPR_INFO(dev,  "FW Loading: .bin\n");

	/* Get TC956X FW binary through kernel firmware interface request */
	if (request_firmware(&pfw, FIRMWARE_NAME, dev) != 0) {
		NMSGPR_ERR(dev,
		"TC956X: Error in calling request_firmware");
		return -EINVAL;
	}

	if (pfw == NULL) {
		NMSGPR_ERR(dev, "TC956X: request_firmware: pfw == NULL");
		return -EINVAL;
	}

	/* Validate the size of the firmware */
	if (pfw->size > TC956X_FW_MAX_SIZE) {
		NMSGPR_ERR(dev, "Error : FW size exceeds the memory size\n");
		return -EINVAL;
	}

	NMSGPR_INFO(dev,  "FW Loading Start...\n");
	NMSGPR_INFO(dev,  "FW Size = %ld\n", (long int)(pfw->size));

	/* Assert M3 reset */
#ifdef TC956X
	adrs = NRSTCTRL0_OFFSET;
#else
	adrs = NRSTCTRL_OFFSET;
#endif
	val = ioread32((void __iomem *)(res->addr + adrs));
	NMSGPR_INFO(dev,  "Reset Register value = %lx\n", (unsigned long)val);

	val |= NRSTCTRL0_RST_ASRT;
	iowrite32(val, (void __iomem *)(res->addr + adrs));

#ifdef TC956X
	iowrite32(0, (void __iomem *)(res->tc956x_SRAM_pci_base_addr +
			TC956X_M3_INIT_DONE));
#endif

#ifdef TC956X
	/* Copy TC956X FW to SRAM */
	memcpy_toio(res->tc956x_SRAM_pci_base_addr, pfw->data, pfw->size);
#endif
	/* Release kernel firmware interface */
	release_firmware(pfw);
#endif

	NMSGPR_INFO(dev,  "FW Loading Finish.\n");

	/* De-assert M3 reset */
#ifdef TC956X
	adrs = NRSTCTRL0_OFFSET;
#else
	adrs = NRSTCTRL_OFFSET;
#endif
	val = ioread32((void __iomem *)(res->addr + adrs));
	val &= ~NRSTCTRL0_RST_DE_ASRT;
	iowrite32(val, (void __iomem *)(res->addr + adrs));

#ifdef TC956X
	readl_poll_timeout_atomic(res->tc956x_SRAM_pci_base_addr + TC956X_M3_INIT_DONE,
				fw_init_sync, fw_init_sync, 100, 100000);
#endif
	if (!fw_init_sync)
		NMSGPR_ALERT(dev, "TC956x FW yet to start!!!");
	else
		NMSGPR_INFO(dev,  "TC956x M3 started.\n");

	return 0;
}

#ifdef DMA_OFFLOAD_ENABLE
/*
 * brief API to populate the table address map registers.
 *
 * details This function pouplates the registers that are used to convert the
 * AXI bus access to PCI TLP.
 *
 * param[in] dev  - pointer to device structure.
 * param[in] id   - pointer to base address of registers.
 * param[in] id	  - pointer to structure containing the TAMAP parameters
 */
void tc956x_config_CM3_tamap(struct device *dev,
				void __iomem *reg_pci_base_addr,
				struct tc956xmac_cm3_tamap *tamap,
				u8 table_entry)
{
#ifdef TC956X
	DBGPR_FUNC(dev, "-->%s\n", __func__);

	writel(TC956X_AXI4_SLV01_TRSL_PARAM_VAL, reg_pci_base_addr +
					TC956X_AXI4_SLV_TRSL_PARAM(0, table_entry));
	writel(tamap->trsl_addr_hi, reg_pci_base_addr +
					TC956X_AXI4_SLV_TRSL_ADDR_HI(0, table_entry));
	writel(tamap->trsl_addr_low, reg_pci_base_addr +
					TC956X_AXI4_SLV_TRSL_ADDR_LO(0, table_entry));
	writel(tamap->src_addr_hi, reg_pci_base_addr +
					TC956X_AXI4_SLV_SRC_ADDR_HI(0, table_entry));
	writel((tamap->src_addr_low & TC956X_SRC_LO_MASK) |
				(tamap->atr_size << 1) | TC956X_ATR_IMPL,
				reg_pci_base_addr + TC956X_AXI4_SLV_SRC_ADDR_LO(0, table_entry));

	KPRINT_INFO("SL0%d TRSL_MASK = 0x%08x\n", table_entry,
		readl(reg_pci_base_addr + TC956X_AXI4_SLV_TRSL_MASK1(0, table_entry)));
	KPRINT_INFO("SL0%d TRSL_MASK = 0x%08x\n", table_entry,
		readl(reg_pci_base_addr + TC956X_AXI4_SLV_TRSL_MASK2(0, table_entry)));
	KPRINT_INFO("SL0%d TRSL_PARAM = 0x%08x\n", table_entry,
		readl(reg_pci_base_addr + TC956X_AXI4_SLV_TRSL_PARAM(0, table_entry)));
	KPRINT_INFO("SL0%d TRSL_ADDR HI = 0x%08x\n", table_entry,
		readl(reg_pci_base_addr + TC956X_AXI4_SLV_TRSL_ADDR_HI(0, table_entry)));
	KPRINT_INFO("SL0%d TRSL_ADDR LO = 0x%08x\n", table_entry,
		readl(reg_pci_base_addr + TC956X_AXI4_SLV_TRSL_ADDR_LO(0, table_entry)));
	KPRINT_INFO("SL0%d SRC_ADDR HI = 0x%08x\n", table_entry,
		readl(reg_pci_base_addr + TC956X_AXI4_SLV_SRC_ADDR_HI(0, table_entry)));
	KPRINT_INFO("SL0%d SRC_ADDR LO = 0x%08x\n", table_entry,
		readl(reg_pci_base_addr + TC956X_AXI4_SLV_SRC_ADDR_LO(0, table_entry)));

#endif
	DBGPR_FUNC(dev, "<--%s\n", __func__);
}
#endif

/*
 * brief API to populate the table address map registers.
 *
 * details This function pouplates the registers that are used to convert the
 * AXI bus access to PCI TLP.
 *
 * param[in] dev  - pointer to device structure.
 * param[in] id   - pointer to base address of registers.
 */
static void tc956x_config_tamap(struct device *dev,
				void __iomem *reg_pci_base_addr)
{
#ifdef TC956X
	DBGPR_FUNC(dev, "-->%s\n", __func__);

	/* AXI4 Slave 0 - Table 0 Entry */
	/* EDMA address region 0x10 0000 0000 - 0x1F FFFF FFFF is
	 * translated to 0x0 0000 0000 - 0xF FFFF FFFF
	 */
	writel(TC956X_AXI4_SLV00_TRSL_PARAM_VAL, reg_pci_base_addr +
					TC956X_AXI4_SLV_TRSL_PARAM(0, 0));
	writel(TC956X_AXI4_SLV00_TRSL_ADDR_HI_VAL, reg_pci_base_addr +
					TC956X_AXI4_SLV_TRSL_ADDR_HI(0, 0));
	writel(TC956X_AXI4_SLV00_TRSL_ADDR_LO_VAL, reg_pci_base_addr +
					TC956X_AXI4_SLV_TRSL_ADDR_LO(0, 0));
	writel(TC956X_AXI4_SLV00_SRC_ADDR_HI_VAL, reg_pci_base_addr +
					TC956X_AXI4_SLV_SRC_ADDR_HI(0, 0));
	writel(TC956X_AXI4_SLV00_SRC_ADDR_LO_VAL |
				TC956X_ATR_SIZE(TC956X_AXI4_SLV00_ATR_SIZE) |
				TC956X_ATR_IMPL, reg_pci_base_addr +
				TC956X_AXI4_SLV_SRC_ADDR_LO(0, 0));

	KPRINT_INFO("SL00 TRSL_MASK = 0x%08x\n",
		readl(reg_pci_base_addr + TC956X_AXI4_SLV_TRSL_MASK1(0, 0)));
	KPRINT_INFO("SL00 TRSL_MASK = 0x%08x\n",
		readl(reg_pci_base_addr + TC956X_AXI4_SLV_TRSL_MASK2(0, 0)));
	KPRINT_INFO("SL00 TRSL_PARAM = 0x%08x\n",
		readl(reg_pci_base_addr + TC956X_AXI4_SLV_TRSL_PARAM(0, 0)));
	KPRINT_INFO("SL00 TRSL_ADDR HI = 0x%08x\n",
		readl(reg_pci_base_addr + TC956X_AXI4_SLV_TRSL_ADDR_HI(0, 0)));
	KPRINT_INFO("SL00 TRSL_ADDR LO = 0x%08x\n",
		readl(reg_pci_base_addr + TC956X_AXI4_SLV_TRSL_ADDR_LO(0, 0)));
	KPRINT_INFO("SL00 SRC_ADDR HI = 0x%08x\n",
		readl(reg_pci_base_addr + TC956X_AXI4_SLV_SRC_ADDR_HI(0, 0)));
	KPRINT_INFO("SL00 SRC_ADDR LO = 0x%08x\n",
		readl(reg_pci_base_addr + TC956X_AXI4_SLV_SRC_ADDR_LO(0, 0)));

#endif
	DBGPR_FUNC(dev, "<--%s\n", __func__);
}

#ifdef TC956X

#ifdef TC956X_PCIE_DISABLE_DSP1
/*
 * brief API to disable dsp1 port
 *
 * details This function sets the registers to disable DSP1 port
 *
 * param[in] dev  - pointer to device structure.
 * param[in] id   - pointer to base address of registers.
 */
static void tc956x_pcie_disable_dsp1_port(struct device *dev,
				void __iomem *reg_sfr_base_addr)
{
	u32 reg_data;
	u32 pcie_mode;

	DBGPR_FUNC(dev, "-->%s\n", __func__);

	/* Read mode setting register
	 * Mode settings values 0:Setting A: x4x1x1, 1:Setting B: x2x2x1
	 */
	reg_data = readl(reg_sfr_base_addr + NMODESTS_OFFSET);
	pcie_mode = (reg_data & NMODESTS_MODE2) >> NMODESTS_MODE2_SHIFT;

	KPRINT_INFO("Pcie mode: %d\n\r", pcie_mode);

	switch (pcie_mode) {
	case TC956X_PCIE_SETTING_A: /* 0:Setting A: x4x1x1 mode */
		/* DSP1 test_in[11] Force receiver detection on all lanes */
		writel(0x00000800, reg_sfr_base_addr + TC956X_GLUE_SW_DSP1_TEST_IN_31_00);
		/*DSP1 is selected*/
		writel(SW_DSP1_ENABLE, reg_sfr_base_addr + TC956X_GLUE_SW_REG_ACCESS_CTRL);
		/*Set 0xFFFF to vendor_id*/
		writel(0xFFFF, reg_sfr_base_addr + TC956X_SSREG_K_PCICONF_015_000);
		/*Set 0xFFFF to device_id*/
		writel(0xFFFF, reg_sfr_base_addr + TC956X_SSREG_K_PCICONF_031_016);
		/*PHY_CORE2 is selected*/
		writel(PHY_CORE_2_ENABLE, reg_sfr_base_addr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
		/*PMACNT_GL_PM_PWRST2_CFG0 setting*/
		writel(0x00000035, reg_sfr_base_addr + TC956X_PHY_COREX_PMACNT_GL_PM_PWRST2_CFG0);
		/*PMACNT_GL_PM_PWRST2_CFG1*/
		writel(0x114F4804, reg_sfr_base_addr + TC956X_PHY_COREX_PMACNT_GL_PM_PWRST2_CFG1);
		/*Set 1 to pm_rxlos_rxei_override_en and Set 0 to pm_rxlos_rxei_override*/
		writel(0x00000010, reg_sfr_base_addr + TC956X_PHY_COREX_PMACNT_LN_PM_LOSCNT_CNT0);
		/*Set 1 to sw_dsp1_global_reset*/
		writel(0x00000010, reg_sfr_base_addr + TC956X_GLUE_SW_RESET_CTRL);
		/*Set 0 to sw_dsp1_global_reset*/
		writel(0x00000000, reg_sfr_base_addr + TC956X_GLUE_SW_RESET_CTRL);
	break;
	case TC956X_PCIE_SETTING_B: /* 1:Setting B: x2x2x1 mode */
		/* DSP1 test_in[11] Force receiver detection on all lanes */
		writel(0x00000800, reg_sfr_base_addr + TC956X_GLUE_SW_DSP1_TEST_IN_31_00);
		/*DSP1 is selected*/
		writel(SW_DSP1_ENABLE, reg_sfr_base_addr + TC956X_GLUE_SW_REG_ACCESS_CTRL);
		/*Set 0xFFFF to vendor_id*/
		writel(0xFFFF, reg_sfr_base_addr + TC956X_SSREG_K_PCICONF_015_000);
		/*Set 0xFFFF to device_id*/
		writel(0xFFFF, reg_sfr_base_addr + TC956X_SSREG_K_PCICONF_031_016);
		/*PHY_CORE1 is selected*/
		writel(PHY_CORE_1_ENABLE, reg_sfr_base_addr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
		/*lane_access_enable*/
		writel(((LANE_1_ENABLE | LANE_0_ENABLE) & LANE_ENABLE_MASK),
					reg_sfr_base_addr + TC956X_PHY_CORE1_GL_LANE_ACCESS);
		/*PMACNT_GL_PM_PWRST2_CFG0 setting*/
		writel(0x00000035, reg_sfr_base_addr + TC956X_PHY_COREX_PMACNT_GL_PM_PWRST2_CFG0);
		/*PMACNT_GL_PM_PWRST2_CFG1*/
		writel(0x114F4804, reg_sfr_base_addr + TC956X_PHY_COREX_PMACNT_GL_PM_PWRST2_CFG1);
		/*Set 1 to pm_rxlos_rxei_override_en and Set 0 to pm_rxlos_rxei_override*/
		writel(0x00000010, reg_sfr_base_addr + TC956X_PHY_COREX_PMACNT_LN_PM_LOSCNT_CNT0);
		/*Set 1 to sw_dsp1_global_reset*/
		writel(0x00000010, reg_sfr_base_addr + TC956X_GLUE_SW_RESET_CTRL);
		/*Set 0 to sw_dsp1_global_reset*/
		writel(0x00000000, reg_sfr_base_addr + TC956X_GLUE_SW_RESET_CTRL);
	break;

	}
	DBGPR_FUNC(dev, "<--%s\n", __func__);
}
#endif /*#ifdef TC956X_PCIE_DISABLE_DSP1*/

#ifdef TC956X_PCIE_DISABLE_DSP2
/*
 * brief API to disable dsp2 port
 *
 * details This function sets the registers to disable DSP2 port
 *
 * param[in] dev  - pointer to device structure.
 * param[in] id   - pointer to base address of registers.
 */
static void tc956x_pcie_disable_dsp2_port(struct device *dev,
				void __iomem *reg_sfr_base_addr)
{
	u32 reg_data;
	u32 pcie_mode;

	DBGPR_FUNC(dev, "-->%s\n", __func__);

	/* Read mode setting register
	 * Mode settings values 0:Setting A: x4x1x1, 1:Setting B: x2x2x1
	 */
	reg_data = readl(reg_sfr_base_addr + NMODESTS_OFFSET);
	pcie_mode = (reg_data & NMODESTS_MODE2) >> NMODESTS_MODE2_SHIFT;

	KPRINT_INFO("Pcie mode: %d\n\r", pcie_mode);

	/*Same settings for both the PCIe modes*/
	/* DSP2 test_in[11] Force receiver detection on all lanes */
	writel(0x00000800, reg_sfr_base_addr + TC956X_GLUE_SW_DSP2_TEST_IN_31_00);
	/*DSP2 is selected*/
	writel(SW_DSP2_ENABLE, reg_sfr_base_addr + TC956X_GLUE_SW_REG_ACCESS_CTRL);
	/*Set 0xFFFF to vendor_id*/
	writel(0xFFFF, reg_sfr_base_addr + TC956X_SSREG_K_PCICONF_015_000);
	/*Set 0xFFFF to device_id*/
	writel(0xFFFF, reg_sfr_base_addr + TC956X_SSREG_K_PCICONF_031_016);
	/*PHY_CORE3 is selected*/
	writel(PHY_CORE_3_ENABLE, reg_sfr_base_addr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
	/*PMACNT_GL_PM_PWRST2_CFG0 setting*/
	writel(0x00000035, reg_sfr_base_addr + TC956X_PHY_COREX_PMACNT_GL_PM_PWRST2_CFG0);
	/*PMACNT_GL_PM_PWRST2_CFG1*/
	writel(0x114F4804, reg_sfr_base_addr + TC956X_PHY_COREX_PMACNT_GL_PM_PWRST2_CFG1);
	/*Set 1 to pm_rxlos_rxei_override_en and Set 0 to pm_rxlos_rxei_override*/
	writel(0x00000010, reg_sfr_base_addr + TC956X_PHY_COREX_PMACNT_LN_PM_LOSCNT_CNT0);
	/*Set 1 to sw_dsp2_global_reset*/
	writel(0x00000100, reg_sfr_base_addr + TC956X_GLUE_SW_RESET_CTRL);
	/*Set 0 to sw_dsp2_global_reset*/
	writel(0x00000000, reg_sfr_base_addr + TC956X_GLUE_SW_RESET_CTRL);

	DBGPR_FUNC(dev, "<--%s\n", __func__);
}
#endif /*#ifdef TC956X_PCIE_DISABLE_DSP2*/

//#ifdef TC956X_PCIE_GEN3_SETTING
static int tc956x_replace_aspm(struct pci_dev *pdev, u16 replace_value, u16 *org_value)
{
	int err;
	u16 lnkctl;

	err = pcie_capability_read_word(pdev, PCI_EXP_LNKCTL, &lnkctl);
	if (err)
		return err;

	if (org_value)
		*org_value = lnkctl & PCI_EXP_LNKCTL_ASPMC;

	lnkctl = (lnkctl & ~PCI_EXP_LNKCTL_ASPMC) | (replace_value & PCI_EXP_LNKCTL_ASPMC);
	err = pcie_capability_write_word(pdev, PCI_EXP_LNKCTL, lnkctl);

	return err;
}

static int tc956x_set_speed(struct pci_dev *pdev, u32 speed)
{
	int ret;
	u16 lnksta, lnkctl, lnkctl_new, lnkctl2, lnkctl2_new;
	u32 lnkcap;
	u32 max_speed = 0, cur_speed = 0, org_speed = 0;

	ret = pcie_capability_read_dword(pdev, PCI_EXP_LNKCAP, &lnkcap);
	if (ret == 0)
		max_speed = (lnkcap & 0xf);

	if (speed > max_speed)
		speed = max_speed;

	ret = pcie_capability_read_word(pdev, PCI_EXP_LNKSTA, &lnksta);
	if (ret == 0)
		org_speed = (lnksta & 0xf);

	ret = pcie_capability_read_word(pdev, PCI_EXP_LNKCTL2, &lnkctl2);
	if (ret == 0) {
		lnkctl2_new = (lnkctl2 & ~PCI_EXP_LNKCTL2_TLS) | speed;
		ret = pcie_capability_write_word(pdev, PCI_EXP_LNKCTL2, lnkctl2_new);
	}

	ret = pcie_capability_read_word(pdev, PCI_EXP_LNKCTL, &lnkctl);
	if (ret == 0) {
		lnkctl_new = (lnkctl | PCI_EXP_LNKCTL_RL);
		ret = pcie_capability_write_word(pdev, PCI_EXP_LNKCTL, lnkctl_new);
	}

	msleep(100);

	ret = pcie_capability_read_word(pdev, PCI_EXP_LNKSTA, &lnksta);
	if (ret == 0) {
		cur_speed = (lnksta & 0xf);
		pci_info(pdev, "Speed changed from Gen.%u to Gen.%u\n", org_speed, cur_speed);
	}

	return ret;
}

static int tc956x_get_speed(struct pci_dev *pdev, u32 *speed)
{
	int ret;
	u16 lnksta;

	if (speed == NULL)
		return 0;

	ret = pcie_capability_read_word(pdev, PCI_EXP_LNKSTA, &lnksta);
	if (ret == 0)
		*speed = (lnksta & 0xf);

	return ret;
}

int tc956x_set_pci_speed(struct pci_dev *pdev, u32 speed)
{
	struct pci_dev *usp;
	struct pci_dev *root;
	struct pci_dev *dsp[3];
	struct pci_dev **devs;
	struct pci_dev *pd;
	int i, j;
	int dev_num;
	u16 *aspm_org;
	int ret = 0;
	u32 cur_speed = 0;

	dsp[2] = pci_upstream_bridge(pdev);
	usp = pci_upstream_bridge(dsp[2]);
	root = pci_upstream_bridge(usp);

	ret = tc956x_get_speed(root, &cur_speed);
	if ((ret == 0) && (cur_speed == speed))
		return 0;

	i = 0;
	for_each_pci_bridge(pd, usp->subordinate)
		dsp[i++] = pd;

	dev_num = 0;
	for (i = 0; i < 3; i++) {
		struct pci_bus *bus = dsp[i]->subordinate;

		if (bus)
			list_for_each_entry(pd, &bus->devices, bus_list)
				dev_num++;
	}

	devs = kcalloc(dev_num, sizeof(struct pci_dev *), GFP_KERNEL);
	if (devs == NULL)
		return -ENOSPC;

	aspm_org = kcalloc(dev_num, sizeof(u16), GFP_KERNEL);
	if (aspm_org == NULL) {
		kfree(devs);
		return -ENOSPC;
	}

	j = 0;
	for (i = 0; i < 3; i++) {
		struct pci_bus *bus = dsp[i]->subordinate;

		if (bus)
			list_for_each_entry(pd, &bus->devices, bus_list)
				devs[j++] = pd;
	}

	/* Save ASPM and set zero */
	for (i = 0; i < dev_num; i++)
		tc956x_replace_aspm(devs[i], 0, &aspm_org[i]);

	tc956x_set_speed(root, speed);
	tc956x_set_speed(dsp[0], speed);
	tc956x_set_speed(dsp[1], speed);
	tc956x_set_speed(dsp[2], speed);
	tc956x_set_speed(root, speed);

	/* Restore ASPM */
	for (i = 0; i < dev_num; i++)
		tc956x_replace_aspm(devs[i], aspm_org[i], NULL);

	kfree(aspm_org);
	kfree(devs);

	return ret;
}
//#endif /*#ifdef TC956X_PCIE_GEN3_SETTING*/
#endif /*#ifdef TC956X*/


/**
 * tc956xmac_pci_probe
 *
 * @pdev: pci device pointer
 * @id: pointer to table of device id/id's.
 *
 * Description: This probing function gets called for all PCI devices which
 * match the ID table and are not "owned" by other driver yet. This function
 * gets passed a "struct pci_dev *" for each device whose entry in the ID table
 * matches the device. The probe functions returns zero when the driver choose
 * to take "ownership" of the device or an error code(-ve no) otherwise.
 */
static int tc956xmac_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *id)
{
	struct tc956xmac_pci_info *info = (struct tc956xmac_pci_info *)id->driver_data;
	struct plat_tc956xmacenet_data *plat;
	struct tc956xmac_resources res;
#ifdef TC956X_PCIE_LINK_STATE_LATENCY_CTRL
	u32 reg_val;
#endif /* end of TC956X_PCIE_LINK_STATE_LATENCY_CTRL */
#ifdef TC956X
	/* use signal from MSPHY */
	uint8_t SgmSigPol = 0;
#ifdef TC956X_PCIE_GEN3_SETTING
	u32 val;
#endif
#endif

	int ret;
	char version_str[32];
#ifdef TC956X_PCIE_LOGSTAT
	struct tc956x_ltssm_log ltssm_data;
#endif/*TC956X_PCIE_LOGSTAT*/
	KPRINT_INFO("%s  >", __func__);
	scnprintf(version_str, sizeof(version_str), "Host Driver Version %d%d-%d%d-%d%d",
		tc956x_drv_version.rel_dbg,
		tc956x_drv_version.major, tc956x_drv_version.minor,
		tc956x_drv_version.sub_minor,
		tc956x_drv_version.patch_rel_major, tc956x_drv_version.patch_rel_minor);
	NMSGPR_INFO(&pdev->dev, "%s\n", version_str);

	plat = devm_kzalloc(&pdev->dev, sizeof(*plat), GFP_KERNEL);
	if (!plat)
		return -ENOMEM;

	plat->mdio_bus_data = devm_kzalloc(&pdev->dev,
					   sizeof(*plat->mdio_bus_data),
					   GFP_KERNEL);
	if (!plat->mdio_bus_data)
		return -ENOMEM;

	plat->dma_cfg = devm_kzalloc(&pdev->dev, sizeof(*plat->dma_cfg),
				     GFP_KERNEL);
	if (!plat->dma_cfg)
		return -ENOMEM;

	/* Enable pci device */
	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "%s: ERROR: failed to enable device\n",
			__func__);
		goto err_out_enb_failed;
	}

	/* Request the PCI IO Memory for the device */
	if (pci_request_regions(pdev, TC956X_RESOURCE_NAME)) {
		NMSGPR_ERR(&(pdev->dev), "%s:Failed to get PCI regions\n",
			TC956X_RESOURCE_NAME);
		ret = -ENODEV;
		DBGPR_FUNC(&(pdev->dev), "<--%s : ret: %d\n", __func__, ret);
		goto err_out_req_reg_failed;
	}

	/* Enable AER Error reporting, if device capability is detected */
	if (pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_ERR)) {

		pci_enable_pcie_error_reporting(pdev);
		NMSGPR_INFO(&(pdev->dev), "AER Capability Enabled\n");
	}

	/* Enable the bus mastering */
	pci_set_master(pdev);

	dev_info(&(pdev->dev),
		"BAR0 length = %lld kb\n", (u64)pci_resource_len(pdev, 0));
	dev_info(&(pdev->dev),
		"BAR2 length = %lld kb\n", (u64)pci_resource_len(pdev, 2));
	dev_info(&(pdev->dev),
		"BAR4 length = %lld kb\n", (u64)pci_resource_len(pdev, 4));
	dev_info(&(pdev->dev),
		"BAR0 iommu address = 0x%llx\n", (u64)pci_resource_start(pdev, 0));
	dev_info(&(pdev->dev),
		"BAR2 iommu address = 0x%llx\n", (u64)pci_resource_start(pdev, 2));
	dev_info(&(pdev->dev),
		"BAR4 iommu address = 0x%llx\n", (u64)pci_resource_start(pdev, 4));

	memset(&res, 0, sizeof(res));
#ifdef TC956X

	res.tc956x_BRIDGE_CFG_pci_base_addr = ioremap_nocache
		(pci_resource_start(pdev, TC956X_BAR0), pci_resource_len(pdev, TC956X_BAR0));

	if (((void __iomem *)res.tc956x_BRIDGE_CFG_pci_base_addr == NULL)) {
		NMSGPR_ERR(&(pdev->dev), "%s: cannot map TC956X BAR0, aborting", pci_name(pdev));
		ret = -EIO;
		DBGPR_FUNC(&(pdev->dev), "<--%s : ret: %d\n", __func__, ret);
		goto err_out_map_failed;
	}

	res.tc956x_SRAM_pci_base_addr = ioremap_nocache
		(pci_resource_start(pdev, TC956X_BAR2), pci_resource_len(pdev, TC956X_BAR2));

	if (((void __iomem *)res.tc956x_SRAM_pci_base_addr == NULL)) {
		pci_iounmap(pdev, (void __iomem *)res.tc956x_BRIDGE_CFG_pci_base_addr);
		NMSGPR_ERR(&(pdev->dev), "%s: cannot map TC956X BAR2, aborting", pci_name(pdev));
		ret = -EIO;
		DBGPR_FUNC(&(pdev->dev), "<--%s : ret: %d\n", __func__, ret);
		goto err_out_map_failed;
	}

	res.tc956x_SFR_pci_base_addr = ioremap_nocache
		(pci_resource_start(pdev, TC956X_BAR4), pci_resource_len(pdev, TC956X_BAR4));

	if (((void __iomem *)res.tc956x_SFR_pci_base_addr == NULL)) {
		pci_iounmap(pdev, (void __iomem *)res.tc956x_BRIDGE_CFG_pci_base_addr);
		pci_iounmap(pdev, (void __iomem *)res.tc956x_SRAM_pci_base_addr);
		NMSGPR_ERR(&(pdev->dev), "%s: cannot map TC956X BAR4, aborting", pci_name(pdev));
		ret = -EIO;
		DBGPR_FUNC(&(pdev->dev), "<--%s : ret: %d\n", __func__, ret);
		goto err_out_map_failed;
	}

	NDBGPR_L1(&(pdev->dev), "BAR0 virtual address = %p\n", res.tc956x_BRIDGE_CFG_pci_base_addr);
	NDBGPR_L1(&(pdev->dev), "BAR2 virtual address = %p\n", res.tc956x_SRAM_pci_base_addr);
	NDBGPR_L1(&(pdev->dev), "BAR4 virtual address = %p\n", res.tc956x_SFR_pci_base_addr);

	res.addr = res.tc956x_SFR_pci_base_addr;
#ifdef TC956X_PCIE_GEN3_SETTING
	val = readl(res.addr + TC956X_GLUE_EFUSE_CTRL);
	if ((val & 0x10) == 0) {
		DBGPR_FUNC(&(pdev->dev), "<--%s : Applying Gen3 setting\n", __func__);
		/* 0x4002C01C SSREG_GLUE_EFUSE_CTRL.pcie_usp_gen3_disable_efuse_ignore */
		writel(0x10, res.addr + TC956X_GLUE_EFUSE_CTRL);
		/* 0x4002C030 All PHY_COREs are selected */
		writel(0x0f, res.addr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
		/* 0x40028000 All Lanes are selected */
		writel(0x0f, res.addr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
		/* 0x4002B268 PMA_LN_PCS2PMA_PHYMODE_R2.pcs2pma_phymode */
		writel(0x02, res.addr + TC956X_PMA_LN_PCS2PMA_PHYMODE_R2);
	}

	if ((tc956x_speed >= 1) && (tc956x_speed <= 3))
		tc956x_set_pci_speed(pdev, tc956x_speed);
#endif

#ifdef TC956X_PCIE_LINK_STATE_LATENCY_CTRL
	/* 0x4002_C02C SSREG_GLUE_SW_REG_ACCESS_CTRL.sw_port_reg_access_enable for USP Access enable */
	writel(SW_USP_ENABLE, res.addr + TC956X_GLUE_SW_REG_ACCESS_CTRL);
	/* 0x4002_496C K_PEXCONF_209_205.aspm_l0s_entry_delay in terms of 256ns */
	writel(USP_L0s_ENTRY_DELAY, res.addr + TC956X_PCIE_S_L0s_ENTRY_LATENCY);
	/* 0x4002_4970 K_PEXCONF_210_219.aspm_L1_entry_delay in terms of 256ns */
	writel(USP_L1_ENTRY_DELAY, res.addr + TC956X_PCIE_S_L1_ENTRY_LATENCY);

	/* 0x4002_C02C SSREG_GLUE_SW_REG_ACCESS_CTRL.sw_port_reg_access_enable for DSP1 Access enable */
	writel(SW_DSP1_ENABLE, res.addr + TC956X_GLUE_SW_REG_ACCESS_CTRL);
	/* 0x4002_496C K_PEXCONF_209_205.aspm_l0s_entry_delay in terms of 256ns */
	writel(DSP1_L0s_ENTRY_DELAY, res.addr + TC956X_PCIE_S_L0s_ENTRY_LATENCY);
	/* 0x4002_4970 K_PEXCONF_210_219.aspm_L1_entry_delay in terms of 256ns */
	writel(DSP1_L1_ENTRY_DELAY, res.addr + TC956X_PCIE_S_L1_ENTRY_LATENCY);

	/* 0x4002_C02C SSREG_GLUE_SW_REG_ACCESS_CTRL.sw_port_reg_access_enable for DSP2 Access enable */
	writel(SW_DSP2_ENABLE, res.addr + TC956X_GLUE_SW_REG_ACCESS_CTRL);
	/* 0x4002_496C K_PEXCONF_209_205.aspm_l0s_entry_delay in terms of 256ns */
	writel(DSP2_L0s_ENTRY_DELAY, res.addr + TC956X_PCIE_S_L0s_ENTRY_LATENCY);
	/* 0x4002_4970 K_PEXCONF_210_219.aspm_L1_entry_delay in terms of 256ns */
	writel(DSP2_L1_ENTRY_DELAY, res.addr + TC956X_PCIE_S_L1_ENTRY_LATENCY);

	/* 0x4002_C02C SSREG_GLUE_SW_REG_ACCESS_CTRL.sw_port_reg_access_enable 
			for VDSP Access enable */
	writel(SW_VDSP_ENABLE, res.addr + TC956X_GLUE_SW_REG_ACCESS_CTRL);
	/* 0x4002_496C K_PEXCONF_209_205.aspm_l0s_entry_delay in terms of 256ns */
	writel(VDSP_L0s_ENTRY_DELAY, res.addr + TC956X_PCIE_S_L0s_ENTRY_LATENCY);
	/* 0x4002_4970 K_PEXCONF_210_219.aspm_L1_entry_delay in terms of 256ns */
	writel(VDSP_L1_ENTRY_DELAY, res.addr + TC956X_PCIE_S_L1_ENTRY_LATENCY);

	/* 0x4002_00D8 Reading PCIE EP Capability setting Register */
	reg_val = readl(res.addr + TC956X_PCIE_EP_CAPB_SET);

	/* Clearing PCIE EP Capability setting of L0s & L1 entry delays */
	reg_val &= ~(TC956X_PCIE_EP_L0s_ENTRY_MASK | TC956X_PCIE_EP_L1_ENTRY_MASK);

	/* Updating PCIE EP Capability setting of L0s & L1 entry delays */
	reg_val |= (((EP_L0s_ENTRY_DELAY << TC956X_PCIE_EP_L0s_ENTRY_SHIFT) &
				TC956X_PCIE_EP_L0s_ENTRY_MASK) | 
			((EP_L1_ENTRY_DELAY << TC956X_PCIE_EP_L1_ENTRY_SHIFT) &
				TC956X_PCIE_EP_L1_ENTRY_MASK));

	/* 0x4002_00D8 PCIE EP Capability setting L0S & L1 entry delay in terms of 256ns */
	writel(reg_val, res.addr + TC956X_PCIE_EP_CAPB_SET);

	/* 0x4002_C02C SSREG_GLUE_SW_REG_ACCESS_CTRL.sw_port_reg_access_enable 
			for All Switch Ports Access enable */
	writel(TC956X_PCIE_S_EN_ALL_PORTS_ACCESS, res.addr + TC956X_GLUE_SW_REG_ACCESS_CTRL);
#endif /* end of TC956X_PCIE_LINK_STATE_LATENCY_CTRL */

#ifdef TC956X_PCIE_DISABLE_DSP1
	tc956x_pcie_disable_dsp1_port(&pdev->dev, res.tc956x_SFR_pci_base_addr);
#endif

#ifdef TC956X_PCIE_DISABLE_DSP2
	tc956x_pcie_disable_dsp2_port(&pdev->dev, res.tc956x_SFR_pci_base_addr);
#endif


#endif

#ifdef TC956X
	res.port_num = readl(res.tc956x_BRIDGE_CFG_pci_base_addr + RSCMNG_ID_REG); /* Resource Manager ID */
	res.port_num &= RSCMNG_PFN;
#endif


#ifdef DISABLE_EMAC_PORT1
#ifdef TC956X
	if (res.port_num == RM_PF1_ID) {

		NMSGPR_ALERT(&pdev->dev, "Disabling all eMAC clocks for Port 1\n");
		/* Disable all clocks to eMAC Port1 */
		ret = readl(res.addr + NCLKCTRL1_OFFSET);

		ret &= (~(NCLKCTRL1_MAC1TXCEN | NCLKCTRL1_MAC1RXCEN |
			  NCLKCTRL1_MAC1ALLCLKEN1 | NCLKCTRL1_MAC1RMCEN));
		writel(ret, res.addr + NCLKCTRL1_OFFSET);

		ret = -ENODEV;
		goto disable_emac_port;
	}
#endif
#endif

	plat->port_num = res.port_num;

	if (res.port_num == RM_PF0_ID) {
		plat->mdc_clk = PORT0_MDC;
		plat->c45_needed = PORT0_C45_STATE;
	}

	if (res.port_num == RM_PF1_ID) {
		plat->mdc_clk = PORT1_MDC;
		plat->c45_needed = PORT1_C45_STATE;
	}

	if (res.port_num == RM_PF0_ID) {
		/* Set the PORT0 interface mode to default, in case of invalid input */
		if ((tc956x_port0_interface ==  ENABLE_RGMII_INTERFACE) ||
		(tc956x_port0_interface >  ENABLE_SGMII_INTERFACE))
			tc956x_port0_interface = ENABLE_XFI_INTERFACE;

		res.port_interface = tc956x_port0_interface;
	}

	if (res.port_num == RM_PF1_ID) {
		/* Set the PORT1 interface mode to default, in case of invalid input */
		if ((tc956x_port1_interface <  ENABLE_RGMII_INTERFACE) ||
		(tc956x_port1_interface >  ENABLE_SGMII_INTERFACE))
			tc956x_port1_interface = ENABLE_SGMII_INTERFACE;

		res.port_interface = tc956x_port1_interface;
	}

	plat->port_interface = res.port_interface;

	ret = info->setup(pdev, plat);

	if (ret)
		return ret;

#ifdef TC956X
	if (res.port_num == RM_PF0_ID) {
		ret = readl(res.addr + NRSTCTRL0_OFFSET);
		ret |= (NRSTCTRL0_INTRST);
		writel(ret, res.addr + NRSTCTRL0_OFFSET);

		ret = readl(res.addr + NCLKCTRL0_OFFSET);
		ret |= NCLKCTRL0_INTCEN;
		writel(ret, res.addr + NCLKCTRL0_OFFSET);

		ret = readl(res.addr + NRSTCTRL0_OFFSET);
		ret &= (~(NRSTCTRL0_INTRST));
		writel(ret, res.addr + NRSTCTRL0_OFFSET);

		/* Configure Address Transslation block
		 * Bridge Base address to be passed for TC956X
		 */
		tc956x_config_tamap(&pdev->dev, res.tc956x_BRIDGE_CFG_pci_base_addr);
	}

#endif

	NMSGPR_INFO(&(pdev->dev), "Initialising eMAC Port %d\n", res.port_num);
	/* Enable MSI Operation */
	ret = pci_enable_msi(pdev);
	if (ret) {
		dev_err(&(pdev->dev),
		"%s:Enable MSI error\n", TC956X_RESOURCE_NAME);
		DBGPR_FUNC(&(pdev->dev), "<--%s : ret: %d\n", __func__, ret);
		goto err_out_msi_failed;
	}

	pci_write_config_dword(pdev, pdev->msi_cap + PCI_MSI_MASK_64, 0);


#ifdef EEPROM_MAC_ADDR

#ifdef TC956X
	iowrite8(EEPROM_OFFSET, (void __iomem *)(res.tc956x_SRAM_pci_base_addr +
			TC956X_M3_SRAM_EEPROM_OFFSET_ADDR));
	iowrite8(EEPROM_MAC_COUNT, (void __iomem *)(res.tc956x_SRAM_pci_base_addr +
			TC956X_M3_SRAM_EEPROM_MAC_COUNT));

#endif

#endif

#ifdef TC956X
	if (res.port_num == RM_PF0_ID) {
		ret = tc956x_load_firmware(&pdev->dev, &res);
		if (ret)
			NMSGPR_ERR(&(pdev->dev), "Firmware load failed\n");
	}
#endif

#ifdef TC956X

	if (res.port_num == RM_PF0_ID) {
		ret = readl(res.addr + NRSTCTRL0_OFFSET);

		/* Assertion of EMAC Port0 software Reset */
		ret |= NRSTCTRL0_MAC0RST;

		writel(ret, res.addr + NRSTCTRL0_OFFSET);

		NMSGPR_ALERT(&pdev->dev, "Enabling all eMAC clocks for Port 0\n");
		/* Enable all clocks to eMAC Port0 */
		ret = readl(res.addr + NCLKCTRL0_OFFSET);

		ret |= ((NCLKCTRL0_MAC0TXCEN | NCLKCTRL0_MAC0ALLCLKEN | NCLKCTRL0_MAC0RXCEN));
		/* Only if "current" port is SGMII 2.5G, configure below clocks. */
		if (res.port_interface == ENABLE_SGMII_INTERFACE) {
			ret &= ~NCLKCTRL0_POEPLLCEN;
			ret &= ~NCLKCTRL0_SGMPCIEN;
			ret &= ~NCLKCTRL0_REFCLKOCEN;
			ret &= ~NCLKCTRL0_MAC0125CLKEN;
			ret &= ~NCLKCTRL0_MAC0312CLKEN;
		}
		writel(ret, res.addr + NCLKCTRL0_OFFSET);

		/* Interface configuration for port0*/
		ret = readl(res.addr + NEMAC0CTL_OFFSET);
		ret &= ~(NEMACCTL_SP_SEL_MASK | NEMACCTL_PHY_INF_SEL_MASK);
		if (res.port_interface == ENABLE_SGMII_INTERFACE)
			ret |= NEMACCTL_SP_SEL_SGMII_2500M;
		else if ((res.port_interface == ENABLE_USXGMII_INTERFACE) ||
			(res.port_interface == ENABLE_XFI_INTERFACE))
			ret |= NEMACCTL_SP_SEL_USXGMII_10G_10G;

		ret &= ~(0x00000040); /* Mask Polarity */
		if (SgmSigPol == 1)
			ret |= 0x00000040; /* Set Active low */

		ret |= NEMACCTL_PHY_INF_SEL | NEMACCTL_LPIHWCLKEN;
		writel(ret, res.addr + NEMAC0CTL_OFFSET);

		/* De-assertion of EMAC Port0  software Reset*/
		ret = readl(res.addr + NRSTCTRL0_OFFSET);
		ret &= ~(NRSTCTRL0_MAC0RST);
		writel(ret, res.addr + NRSTCTRL0_OFFSET);
	}

	if (res.port_num == RM_PF1_ID) {
		ret = readl(res.addr + NRSTCTRL1_OFFSET);

		/* Assertion of EMAC Port1 software Reset*/
		ret |= NRSTCTRL1_MAC1RST1;
		writel(ret, res.addr + NRSTCTRL1_OFFSET);

		NMSGPR_ALERT(&pdev->dev, "Enabling all eMAC clocks for Port 1\n");
		/* Enable all clocks to eMAC Port1 */
		ret = readl(res.addr + NCLKCTRL1_OFFSET);

		ret |= ((NCLKCTRL1_MAC1TXCEN | NCLKCTRL1_MAC1RXCEN |
		NCLKCTRL1_MAC1ALLCLKEN1 | 1 << 15));
		if (res.port_interface == ENABLE_SGMII_INTERFACE) {
			ret &= ~NCLKCTRL1_MAC1125CLKEN1;
			ret &= ~NCLKCTRL1_MAC1312CLKEN1;
		}
		writel(ret, res.addr + NCLKCTRL1_OFFSET);

		/* Interface configuration for port1*/
		ret = readl(res.addr + NEMAC1CTL_OFFSET);
		ret &= ~(NEMACCTL_SP_SEL_MASK | NEMACCTL_PHY_INF_SEL_MASK);
		if (res.port_interface == ENABLE_RGMII_INTERFACE)
			ret |= NEMACCTL_SP_SEL_RGMII_1000M;
		else if (res.port_interface == ENABLE_SGMII_INTERFACE)
			ret |= NEMACCTL_SP_SEL_SGMII_2500M;
		else if ((res.port_interface == ENABLE_USXGMII_INTERFACE) ||
			(res.port_interface == ENABLE_XFI_INTERFACE))
			ret |= NEMACCTL_SP_SEL_USXGMII_10G_10G;

		ret &= ~(0x00000040); /* Mask Polarity */
		if (SgmSigPol == 1)
			ret |= 0x00000040; /* Set Active low */

		ret |= NEMACCTL_PHY_INF_SEL | NEMACCTL_LPIHWCLKEN;
		writel(ret, res.addr + NEMAC1CTL_OFFSET);

		/* De-assertion of EMAC Port1  software Reset */
		ret = readl(res.addr + NRSTCTRL1_OFFSET);
		ret &= ~NRSTCTRL1_MAC1RST1;
		writel(ret, res.addr + NRSTCTRL1_OFFSET);
	}
#endif

	res.wol_irq = pdev->irq;
	res.irq = pdev->irq;
	res.lpi_irq = pdev->irq;

	plat->bus_id = res.port_num;

	ret = tc956xmac_dvr_probe(&pdev->dev, plat, &res);
	if (ret) {
		if (ret == -ENODEV) {
			dev_info(&(pdev->dev), "Port%d will be registered as PCIe device only", res.port_num);
			/* Make sure probe() succeeds by returning 0 to caller of probe() */
			ret = 0;
		} else {
			dev_err(&(pdev->dev), "<--%s : ret: %d\n", __func__, ret);
			goto err_dvr_probe;
		}
	}
#ifdef TC956X
	if ((res.port_num == RM_PF1_ID) && (res.port_interface == ENABLE_RGMII_INTERFACE)) {
		writel(0x00000000, res.addr + 0x1050);
		writel(0xF300F300, res.addr + 0x107C);
	}
#endif

#ifdef TC956X_PCIE_LOGSTAT
	memset(&ltssm_data, 0, sizeof(ltssm_data));
	ret = tc956x_logstat_GetLTSSMLogData((void __iomem *)res.addr, UPSTREAM_PORT, &ltssm_data);
	if (ret == 0) {
		dev_dbg(&(pdev->dev), "%s : ltssm_data.eq_phase          = %d\n", __func__, ltssm_data.eq_phase);
		dev_dbg(&(pdev->dev), "%s : ltssm_data.rxL0s             = %d\n", __func__, ltssm_data.rxL0s);
		dev_dbg(&(pdev->dev), "%s : ltssm_data.txL0s             = %d\n", __func__, ltssm_data.txL0s);
		dev_dbg(&(pdev->dev), "%s : ltssm_data.substate_L1       = %d\n", __func__, ltssm_data.substate_L1);
		dev_dbg(&(pdev->dev), "%s : ltssm_data.active_lane;      = %d\n", __func__, ltssm_data.active_lane);
		dev_dbg(&(pdev->dev), "%s : ltssm_data.link_speed        = %d\n", __func__, ltssm_data.link_speed);
		dev_dbg(&(pdev->dev), "%s : ltssm_data.dl_active         = %d\n", __func__, ltssm_data.dl_active);
		dev_dbg(&(pdev->dev), "%s : ltssm_data.ltssm_timeout     = %d\n", __func__, ltssm_data.ltssm_timeout);
		dev_dbg(&(pdev->dev), "%s : ltssm_data.ltssm_stop_status = %d\n", __func__, ltssm_data.ltssm_stop_status);
	}
#endif /* TC956X_PCIE_LOGSTAT */

#ifdef DMA_OFFLOAD_ENABLE
	if (res.port_num == RM_PF0_ID)
		port0_pdev = pdev;
#endif
	return ret;

err_out_msi_failed:
err_dvr_probe:
	pci_disable_msi(pdev);
#ifdef DISABLE_EMAC_PORT1
disable_emac_port:
#endif
#ifdef TC956X
	if (((void __iomem *)res.tc956x_SFR_pci_base_addr != NULL))
		pci_iounmap(pdev, (void __iomem *)res.tc956x_SFR_pci_base_addr);
	if (((void __iomem *)res.tc956x_SRAM_pci_base_addr != NULL))
		pci_iounmap(pdev, (void __iomem *)res.tc956x_SRAM_pci_base_addr);
	if (((void __iomem *)res.tc956x_BRIDGE_CFG_pci_base_addr != NULL))
		pci_iounmap(pdev, (void __iomem *)res.tc956x_BRIDGE_CFG_pci_base_addr);
#endif
err_out_map_failed:
	pci_release_regions(pdev);
err_out_req_reg_failed:
	pci_disable_device(pdev);
err_out_enb_failed:
	return ret;

}

/**
 * tc956x_pci_remove
 *
 * \brief API to release all the resources from the driver.
 *
 * \details The remove function gets called whenever a device being handled
 * by this driver is removed (either during deregistration of the driver or
 * when it is manually pulled out of a hot-pluggable slot). This function
 * should reverse operations performed at probe time. The remove function
 * always gets called from process context, so it can sleep.
 *
 * \param[in] pdev - pointer to pci_dev structure.
 *
 * \return void
 */
static void tc956xmac_pci_remove(struct pci_dev *pdev)
{
	struct net_device *ndev = dev_get_drvdata(&pdev->dev);
	struct tc956xmac_priv *priv = netdev_priv(ndev);

	DBGPR_FUNC(&(pdev->dev), "-->%s\n", __func__);

#ifdef DMA_OFFLOAD_ENABLE
	if (priv->port_num == RM_PF0_ID)
		port0_pdev = NULL;
#endif
	/* phy_addr == -1 indicates that PHY was not found and
	 * device is registered as only PCIe device. So skip any
	 * ethernet device related uninitialization
	 */
	if (priv->plat->phy_addr != -1)
		tc956xmac_dvr_remove(&pdev->dev);

	pdev->irq = 0;

	if (tc956x_platform_remove(priv)) {
		dev_err(priv->device, "Platform remove error\n");
	}

	/* Enable MSI Operation */
	pci_disable_msi(pdev);

	if (priv->plat->tc956xmac_clk)
		clk_unregister_fixed_rate(priv->plat->tc956xmac_clk);

#ifdef TC956X
	/* Un-map previously mapped BAR0/2/4 address memory */
	if ((void __iomem *)priv->tc956x_SFR_pci_base_addr != NULL)
		pci_iounmap(pdev, (void __iomem *)
			priv->tc956x_SFR_pci_base_addr);
	if ((void __iomem *)priv->tc956x_SRAM_pci_base_addr != NULL)
		pci_iounmap(pdev, (void __iomem *)
			priv->tc956x_SRAM_pci_base_addr);
	if ((void __iomem *)priv->tc956x_BRIDGE_CFG_pci_base_addr != NULL)
		pci_iounmap(pdev, (void __iomem *)
			priv->tc956x_BRIDGE_CFG_pci_base_addr);
#endif
	pci_release_regions(pdev);

	pci_disable_device(pdev);

	DBGPR_FUNC(&(pdev->dev), "<--%s\n", __func__);
}


/*!
 * \brief Routine to put the device in suspend mode
 *
 * \details This function gets called by PCI core when the device is being
 * suspended. The suspended state is passed as input argument to it. This
 * function saves the configuration space of the device and puts the device
 * in the requested suspend state apart from invoking tc956xmac_suspend() to
 * process MAC related suspend operations.
 *
 * \param[in] pdev \96 pointer to pci device structure.
 * \param[in] state \96 suspend state of device.
 *
 * \return s32
 *
 * \retval 0
 */
static s32 tc956x_pcie_suspend(struct pci_dev *pdev, pm_message_t state)
{
	s32 ret = 0;
	struct net_device *ndev = dev_get_drvdata(&pdev->dev);
	struct tc956xmac_priv *priv = netdev_priv(ndev);
#ifdef DMA_OFFLOAD_ENABLE
	u8 i;
	u32 val;
#endif

	DBGPR_FUNC(&(pdev->dev), "-->%s\n", __func__);

	/* Call tc956xmac_suspend() */
	tc956xmac_suspend(&pdev->dev);

#ifdef DMA_OFFLOAD_ENABLE
	if (priv->port_num == RM_PF0_ID) {
		/* Since TAMAP is common for Port0 and Port1,
		 * Store CM3 TAMAP entries of one Port0*/
		for (i = 1; i <= MAX_CM3_TAMAP_ENTRIES; i++) {
			priv->cm3_tamap[i-1].valid = false;

			val = readl(priv->tc956x_BRIDGE_CFG_pci_base_addr + TC956X_AXI4_SLV_SRC_ADDR_LO(0, i));
			if (((val & TC956X_ATR_SIZE_MASK) >> TC956x_ATR_SIZE_SHIFT) != 0x3F) {
				priv->cm3_tamap[i-1].trsl_addr_hi = readl(priv->tc956x_BRIDGE_CFG_pci_base_addr +
									TC956X_AXI4_SLV_TRSL_ADDR_HI(0, i));
				priv->cm3_tamap[i-1].trsl_addr_low = readl(priv->tc956x_BRIDGE_CFG_pci_base_addr +
									TC956X_AXI4_SLV_TRSL_ADDR_LO(0, i));
				priv->cm3_tamap[i-1].src_addr_hi = readl(priv->tc956x_BRIDGE_CFG_pci_base_addr +
									TC956X_AXI4_SLV_SRC_ADDR_HI(0, i));
				priv->cm3_tamap[i-1].src_addr_low = readl(priv->tc956x_BRIDGE_CFG_pci_base_addr +
									TC956X_AXI4_SLV_SRC_ADDR_LO(0, i)) & TC956X_SRC_LO_MASK;
				priv->cm3_tamap[i-1].atr_size = (readl(priv->tc956x_BRIDGE_CFG_pci_base_addr +
									TC956X_AXI4_SLV_SRC_ADDR_LO(0, i)) & TC956X_ATR_SIZE_MASK) >> TC956x_ATR_SIZE_SHIFT;
				priv->cm3_tamap[i-1].valid = true;
			}
		}
	}
#endif

	ret = tc956x_platform_suspend(priv);
	if (ret) {
		NMSGPR_ERR(&(pdev->dev), "%s: error in calling tc956x_platform_suspend", pci_name(pdev));
		return ret;
	}

	/* Save the PCI Config Space of the device */
	ret = pci_save_state(pdev);

	if (ret) {
		NMSGPR_ERR(&(pdev->dev),
		"%s: error in calling pci_save_state", pci_name(pdev));
		DBGPR_FUNC(&(pdev->dev), "<--%s\n", __func__);
		return ret;
	}

	/* Set the device into the requested power state */
	ret = pci_set_power_state(pdev, pci_choose_state(pdev, state));

	if (ret) {
		NMSGPR_ERR(&(pdev->dev),
		"%s: error in calling pci_set_power_state", pci_name(pdev));
		DBGPR_FUNC(&(pdev->dev), "<--%s\n", __func__);
		return ret;
	}

	DBGPR_FUNC(&(pdev->dev), "<--%s\n", __func__);

	return 0;
}

/*!
 * \brief Routine to configure device during resume
 *
 * \details This function gets called by PCI core when the device is being
 * resumed. It is always called after suspend has been called. These function
 * reverse operations performed at suspend time. This function configure emac
 * port 0, 1 and xpcs to perform MAC realted resume operations.
 *
 * \param[in] pdev pointer to pci device structure.
 *
 * \return s32
 *
 * \retval 0
 */
#ifdef TC956X
static int tc956x_pcie_resume_config(struct pci_dev *pdev)
{
	struct net_device *ndev = dev_get_drvdata(&pdev->dev);
	struct tc956xmac_priv *priv = netdev_priv(ndev);
	/* use signal from MSPHY */
	uint8_t SgmSigPol = 0;
	int ret = 0;

	if (priv->port_num == RM_PF0_ID) {
		ret = readl(priv->tc956x_SFR_pci_base_addr + NRSTCTRL0_OFFSET);

		/* Assertion of EMAC Port0 software Reset */
		ret |= NRSTCTRL0_MAC0RST;

		writel(ret, priv->tc956x_SFR_pci_base_addr + NRSTCTRL0_OFFSET);

		NMSGPR_ALERT(&pdev->dev, "Enabling all eMAC clocks for Port 0\n");
		/* Enable all clocks to eMAC Port0 */
		ret = readl(priv->tc956x_SFR_pci_base_addr + NCLKCTRL0_OFFSET);

		ret |= ((NCLKCTRL0_MAC0TXCEN | NCLKCTRL0_MAC0ALLCLKEN | NCLKCTRL0_MAC0RXCEN));
		if (priv->port_interface == ENABLE_SGMII_INTERFACE) {
			/* Disable Clocks for 2.5Gbps SGMII */
			ret &= ~NCLKCTRL0_POEPLLCEN;
			ret &= ~NCLKCTRL0_SGMPCIEN;
			ret &= ~NCLKCTRL0_REFCLKOCEN;
			ret &= ~NCLKCTRL0_MAC0125CLKEN;
			ret &= ~NCLKCTRL0_MAC0312CLKEN;
		}
		writel(ret, priv->tc956x_SFR_pci_base_addr + NCLKCTRL0_OFFSET);

		/* Interface configuration for port0*/
		ret = readl(priv->tc956x_SFR_pci_base_addr + NEMAC0CTL_OFFSET);
		ret &= ~(NEMACCTL_SP_SEL_MASK | NEMACCTL_PHY_INF_SEL_MASK);
		if (priv->port_interface == ENABLE_SGMII_INTERFACE)
			ret |= NEMACCTL_SP_SEL_SGMII_2500M;
		else if ((priv->port_interface == ENABLE_USXGMII_INTERFACE) ||
			(priv->port_interface == ENABLE_XFI_INTERFACE))
			ret |= NEMACCTL_SP_SEL_USXGMII_10G_10G;

		ret &= ~(0x00000040); /* Mask Polarity */
		if (SgmSigPol == 1)
			ret |= 0x00000040; /* Set Active low */

		ret |= NEMACCTL_PHY_INF_SEL | NEMACCTL_LPIHWCLKEN;
		writel(ret, priv->tc956x_SFR_pci_base_addr + NEMAC0CTL_OFFSET);

		/* De-assertion of EMAC Port0  software Reset*/
		ret = readl(priv->tc956x_SFR_pci_base_addr + NRSTCTRL0_OFFSET);
		ret &= ~(NRSTCTRL0_MAC0RST);
		writel(ret, priv->tc956x_SFR_pci_base_addr + NRSTCTRL0_OFFSET);
	}

	if (priv->port_num == RM_PF1_ID) {
		ret = readl(priv->tc956x_SFR_pci_base_addr + NRSTCTRL1_OFFSET);

		/* Assertion of EMAC Port1 software Reset*/
		ret |= NRSTCTRL1_MAC1RST1;
		writel(ret, priv->tc956x_SFR_pci_base_addr + NRSTCTRL1_OFFSET);

		NMSGPR_ALERT(&pdev->dev, "Enabling all eMAC clocks for Port 1\n");
		/* Enable all clocks to eMAC Port1 */
		ret = readl(priv->tc956x_SFR_pci_base_addr + NCLKCTRL1_OFFSET);

		ret |= ((NCLKCTRL1_MAC1TXCEN | NCLKCTRL1_MAC1RXCEN |
		NCLKCTRL1_MAC1ALLCLKEN1 | 1 << 15));
		if (priv->port_interface == ENABLE_SGMII_INTERFACE) {
			ret &= ~NCLKCTRL1_MAC1125CLKEN1;
			ret &= ~NCLKCTRL1_MAC1312CLKEN1;
		}
		writel(ret, priv->tc956x_SFR_pci_base_addr + NCLKCTRL1_OFFSET);

		/* Interface configuration for port1*/
		ret = readl(priv->tc956x_SFR_pci_base_addr + NEMAC1CTL_OFFSET);
		ret &= ~(NEMACCTL_SP_SEL_MASK | NEMACCTL_PHY_INF_SEL_MASK);
		if (priv->port_interface == ENABLE_RGMII_INTERFACE)
			ret |= NEMACCTL_SP_SEL_RGMII_1000M;
		else if (priv->port_interface == ENABLE_SGMII_INTERFACE)
			ret |= NEMACCTL_SP_SEL_SGMII_2500M;
		else if ((priv->port_interface == ENABLE_USXGMII_INTERFACE) ||
			(priv->port_interface == ENABLE_XFI_INTERFACE))
			ret |= NEMACCTL_SP_SEL_USXGMII_10G_10G;

		ret &= ~(0x00000040); /* Mask Polarity */
		if (SgmSigPol == 1)
			ret |= 0x00000040; /* Set Active low */

		ret |= NEMACCTL_PHY_INF_SEL | NEMACCTL_LPIHWCLKEN;
		writel(ret, priv->tc956x_SFR_pci_base_addr + NEMAC1CTL_OFFSET);

		/* De-assertion of EMAC Port1  software Reset */
		ret = readl(priv->tc956x_SFR_pci_base_addr + NRSTCTRL1_OFFSET);
		ret &= ~NRSTCTRL1_MAC1RST1;
		writel(ret, priv->tc956x_SFR_pci_base_addr + NRSTCTRL1_OFFSET);
	}

/*PMA module init*/
	if (priv->hw->xpcs) {

		if (priv->port_num == RM_PF0_ID) {
			/* Assertion of PMA &  XPCS reset  software Reset*/
			ret = readl(priv->ioaddr + NRSTCTRL0_OFFSET);
			ret |= (NRSTCTRL0_MAC0PMARST | NRSTCTRL0_MAC0PONRST);
			writel(ret, priv->ioaddr + NRSTCTRL0_OFFSET);
		}

		if (priv->port_num == RM_PF1_ID) {
			/* Assertion of PMA &  XPCS reset  software Reset*/
			ret = readl(priv->ioaddr + NRSTCTRL1_OFFSET);
			ret |= (NRSTCTRL1_MAC1PMARST1 | NRSTCTRL1_MAC1PONRST1);
			writel(ret, priv->ioaddr + NRSTCTRL1_OFFSET);
		}

		ret = tc956x_pma_setup(priv, priv->pmaaddr);
		if (ret < 0)
			KPRINT_INFO("PMA switching to internal clock Failed\n");

		if (priv->port_num == RM_PF0_ID) {
			/* De-assertion of PMA &  XPCS reset  software Reset*/
			ret = readl(priv->ioaddr + NRSTCTRL0_OFFSET);
			ret &= ~(NRSTCTRL0_MAC0PMARST | NRSTCTRL0_MAC0PONRST);
			writel(ret, priv->ioaddr + NRSTCTRL0_OFFSET);
		}

		if (priv->port_num == RM_PF1_ID) {
			/* De-assertion of PMA &  XPCS reset  software Reset*/
			ret = readl(priv->ioaddr + NRSTCTRL1_OFFSET);
			ret &= ~(NRSTCTRL1_MAC1PMARST1 | NRSTCTRL1_MAC1PONRST1);
			writel(ret, priv->ioaddr + NRSTCTRL1_OFFSET);
		}

		if (priv->port_num == RM_PF0_ID) {
			do {
				ret = readl(priv->ioaddr + NEMAC0CTL_OFFSET);
		} while ((NEMACCTL_INIT_DONE & ret) != NEMACCTL_INIT_DONE);
		}

		if (priv->port_num == RM_PF1_ID) {
			do {
				ret = readl(priv->ioaddr + NEMAC1CTL_OFFSET);
		} while ((NEMACCTL_INIT_DONE & ret) != NEMACCTL_INIT_DONE);
		}
		ret = tc956x_xpcs_init(priv, priv->xpcsaddr);
		if (ret < 0)
			KPRINT_INFO("XPCS initialization error\n");
	}

	return ret;
}
#endif
/*!
 * \brief Routine to resume device operation
 *
 * \details This function gets called by PCI core when the device is being
 * resumed. It is always called after suspend has been called. These function
 * reverse operations performed at suspend time. This function restores the
 * power state of the device and restores the PCI config space apart from
 * invoking tc956xmac_resume() to perform MAC realted resume operations.
 *
 * \param[in] pdev pointer to pci device structure.
 *
 * \return s32
 *
 * \retval 0
 */

static s32 tc956x_pcie_resume(struct pci_dev *pdev)
{
	struct net_device *ndev = dev_get_drvdata(&pdev->dev);
	struct tc956xmac_priv *priv = netdev_priv(ndev);
	s32 ret = 0;
#ifdef DMA_OFFLOAD_ENABLE
	u8 i;
#endif

#ifdef TC956X_PCIE_LOGSTAT
	struct tc956x_ltssm_log ltssm_data;
#endif /* TC956X_PCIE_LOGSTAT */
	DBGPR_FUNC(&(pdev->dev), "-->%s\n", __func__);

	/* Set requested power state */
	ret = pci_set_power_state(pdev, PCI_D0);
	if (ret) {
		NMSGPR_ERR(&(pdev->dev),
		"%s: error in calling pci_set_power_state", pci_name(pdev));
		DBGPR_FUNC(&(pdev->dev), "<--%s\n", __func__);
		return ret;
	}

	/* Restore PCI config space of device */
	pci_restore_state(pdev);

	ret = tc956x_platform_resume(priv);
	if (ret) {
		NMSGPR_ERR(&(pdev->dev), "%s: error in calling tc956x_platform_resume", pci_name(pdev));
		pci_disable_device(pdev);
		return ret;
	}

	/* Configure TA map registers */
#ifdef TC956X
	if (priv->port_num == RM_PF0_ID) {
		tc956x_config_tamap(&pdev->dev, priv->tc956x_BRIDGE_CFG_pci_base_addr);
#ifdef DMA_OFFLOAD_ENABLE
		for (i = 1; i <= MAX_CM3_TAMAP_ENTRIES; i++) {
			if (priv->cm3_tamap[i-1].valid)
				tc956x_config_CM3_tamap(&pdev->dev, priv->tc956x_BRIDGE_CFG_pci_base_addr,
							&priv->cm3_tamap[i-1], i);
		}

#endif
	}
#endif
#ifdef TC956X
	/* Configure EMAC Port */
	tc956x_pcie_resume_config(pdev);
#endif
	/* Call tc956xmac_resume() */
	tc956xmac_resume(&pdev->dev);
#ifdef TC956X
	if ((priv->port_num == RM_PF1_ID) && (priv->port_interface == ENABLE_RGMII_INTERFACE)) {
		writel(NEMACTXCDLY_DEFAULT, priv->ioaddr + TC9563_CFG_NEMACTXCDLY);
		writel(NEMACIOCTL_DEFAULT, priv->ioaddr + TC9563_CFG_NEMACIOCTL);
	}
#endif
	DBGPR_FUNC(&(pdev->dev), "<--%s\n", __func__);

#ifdef TC956X_PCIE_LOGSTAT
	memset(&ltssm_data, 0, sizeof(ltssm_data));
	ret = tc956x_logstat_GetLTSSMLogData((void __iomem *)priv->ioaddr, UPSTREAM_PORT, &ltssm_data);
	if (ret == 0) {
		dev_dbg(&(pdev->dev), "%s : ltssm_data.eq_phase          = %d\n", __func__, ltssm_data.eq_phase);
		dev_dbg(&(pdev->dev), "%s : ltssm_data.rxL0s             = %d\n", __func__, ltssm_data.rxL0s);
		dev_dbg(&(pdev->dev), "%s : ltssm_data.txL0s             = %d\n", __func__, ltssm_data.txL0s);
		dev_dbg(&(pdev->dev), "%s : ltssm_data.substate_L1       = %d\n", __func__, ltssm_data.substate_L1);
		dev_dbg(&(pdev->dev), "%s : ltssm_data.active_lane;      = %d\n", __func__, ltssm_data.active_lane);
		dev_dbg(&(pdev->dev), "%s : ltssm_data.link_speed        = %d\n", __func__, ltssm_data.link_speed);
		dev_dbg(&(pdev->dev), "%s : ltssm_data.dl_active         = %d\n", __func__, ltssm_data.dl_active);
		dev_dbg(&(pdev->dev), "%s : ltssm_data.ltssm_timeout     = %d\n", __func__, ltssm_data.ltssm_timeout);
		dev_dbg(&(pdev->dev), "%s : ltssm_data.ltssm_stop_status = %d\n", __func__, ltssm_data.ltssm_stop_status);
	}
#endif /* TC956X_PCIE_LOGSTAT */
	return ret;
}

/*!
 * \brief API to shutdown the device.
 *
 * \details This is a dummy implementation for the shutdown feature of the
 * pci_driver structure.
 *
 * \param[in] pdev - pointer to pci_dev structure.
 */
static void tc956x_pcie_shutdown(struct pci_dev *pdev)
{
	DBGPR_FUNC(&(pdev->dev), "-->%s\n", __func__);
	NMSGPR_ALERT(&(pdev->dev), "Handle the shutdown\n");
	DBGPR_FUNC(&(pdev->dev), "<--%s\n", __func__);
}

/**
 * tc956x_pcie_error_detected
 *
 * \brief Function is called when PCI AER kernel module detects an error.
 *
 * \details This is a dummy implementation for the callback registration
 *
 * \param[in] pdev - pointer to pci_dev structure.
 *
 * \param[in] state - PCI error state.
 *
 * \return Error recovery state
 */
static pci_ers_result_t tc956x_pcie_error_detected(struct pci_dev *pdev,
						pci_channel_state_t state)
{
	NMSGPR_ERR(&(pdev->dev), "PCI AER Error detected : %d\n", state);

	/* No further error recovery to be carried out */
	return PCI_ERS_RESULT_DISCONNECT;
}

/**
 * tc956x_pcie_slot_reset
 *
 * \brief Function is called when PCI AER kernel module issues an slot reset.
 *
 * \details This is a dummy implementation for the callback registration
 *
 * \param[in] pdev - pointer to pci_dev structure.
 *
 * \return Error recovery state
 */
static pci_ers_result_t tc956x_pcie_slot_reset(struct pci_dev *pdev)
{
	NMSGPR_ERR(&(pdev->dev), "PCI AER Slot reset Invoked\n");

	/* No further error recovery to be carried out */
	return PCI_ERS_RESULT_DISCONNECT;
}

/**
 * tc956x_pcie_io_resume
 *
 * \brief Function is called when PCI AER kernel module requests for
 *	  device to resume.
 *
 * \details This is a dummy implementation for the callback registration
 *
 * \param[in] pdev - pointer to pci_dev structure.
 *
 * \return void
 */
static void tc956x_pcie_io_resume(struct pci_dev *pdev)
{
	NMSGPR_ERR(&(pdev->dev), "PCI AER Resume Invoked\n");
}

/* PCI AER Error handlers */
static struct pci_error_handlers tc956x_err_handler = {
	.error_detected = tc956x_pcie_error_detected,
	.slot_reset = tc956x_pcie_slot_reset,
	.resume = tc956x_pcie_io_resume,
};

/* synthetic ID, no official vendor */
#define PCI_VENDOR_ID_TC956XMAC 0x700

#define TC956XMAC_QUARK_ID  0x0937
#define TC956XMAC_DEVICE_ID 0x1108
#define TC956XMAC_EHL_RGMII1G_ID	0x4b30
#define TC956XMAC_EHL_SGMII1G_ID	0x4b31
#define TC956XMAC_TGL_SGMII1G_ID	0xa0ac
#define TC956XMAC_GMAC5_ID		0x7102
#define TC956XMAC_XGMAC3_10G	0x7203
#define TC956XMAC_XGMAC3_2_5G	0x7207
#define TC956XMAC_XGMAC3_2_5G_MDIO	0x7211

#define TC956XMAC_DEVICE(vendor_id, dev_id, info)	{	\
	PCI_VDEVICE(vendor_id, dev_id),			\
	.driver_data = (kernel_ulong_t)&info		\
	}

static const struct pci_device_id tc956xmac_id_table[] = {
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
	TC956XMAC_DEVICE(TC956XMAC, TC956XMAC_DEVICE_ID, tc956xmac_pci_info),
	TC956XMAC_DEVICE(STMICRO, PCI_DEVICE_ID_STMICRO_MAC, tc956xmac_pci_info),
	TC956XMAC_DEVICE(INTEL, TC956XMAC_QUARK_ID, quark_pci_info),
	TC956XMAC_DEVICE(INTEL, TC956XMAC_EHL_RGMII1G_ID, ehl_rgmii1g_pci_info),
	TC956XMAC_DEVICE(INTEL, TC956XMAC_EHL_SGMII1G_ID, ehl_sgmii1g_pci_info),
	TC956XMAC_DEVICE(INTEL, TC956XMAC_TGL_SGMII1G_ID, tgl_sgmii1g_pci_info),
	TC956XMAC_DEVICE(SYNOPSYS, TC956XMAC_GMAC5_ID, snps_gmac5_pci_info),
	TC956XMAC_DEVICE(SYNOPSYS, TC956XMAC_XGMAC3_10G, tc956xmac_xgmac3_pci_info),
	TC956XMAC_DEVICE(SYNOPSYS, TC956XMAC_XGMAC3_2_5G, tc956xmac_xgmac3_2_5g_pci_info),
	TC956XMAC_DEVICE(SYNOPSYS, TC956XMAC_XGMAC3_2_5G_MDIO,
		      tc956xmac_xgmac3_2_5g_mdio_pci_info),
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */
#ifdef TC956X
	TC956XMAC_DEVICE(TOSHIBA, DEVICE_ID, tc956xmac_xgmac3_pci_info),
#endif
	{}
};


static struct pci_driver tc956xmac_pci_driver = {
	.name = TC956X_RESOURCE_NAME,
	.id_table = tc956xmac_id_table,
	.probe = tc956xmac_pci_probe,
	.remove = tc956xmac_pci_remove,
	.shutdown	= tc956x_pcie_shutdown,
#ifdef CONFIG_PM
	.suspend	= tc956x_pcie_suspend,
	.resume		= tc956x_pcie_resume,
#endif
	.driver		= {
		.name		= TC956X_RESOURCE_NAME,
		.owner		= THIS_MODULE,
	},
	.err_handler = &tc956x_err_handler
};


/*!
 * \brief API to register the driver.
 *
 * \details This is the first function called when the driver is loaded.
 * It register the driver with PCI sub-system
 *
 * \return void.
 */
static s32 __init tc956x_init_module(void)
{
	s32 ret = 0;

	KPRINT_INFO("%s", __func__);
	ret = pci_register_driver(&tc956xmac_pci_driver);
	if (ret) {
		KPRINT_INFO("TC956X : Driver registration failed");
		return ret;
	}

	tc956xmac_init();
	KPRINT_INFO("%s", __func__);
	return ret;
}

/*!
 * \brief API to unregister the driver.
 *
 * \details This is the first function called when the driver is removed.
 * It unregister the driver from PCI sub-system
 *
 * \return void.
 */
static void __exit tc956x_exit_module(void)
{
	KPRINT_INFO("%s", __func__);
	pci_unregister_driver(&tc956xmac_pci_driver);
	tc956xmac_exit();
	KPRINT_INFO("%s", __func__);
}

/*!
 * \brief Macro to register the driver registration function.
 *
 * \details A module always begin with either the init_module or the function
 * you specify with module_init call. This is the entry function for modules;
 * it tells the kernel what functionality the module provides and sets up the
 * kernel to run the module's functions when they're needed. Once it does this,
 * entry function returns and the module does nothing until the kernel wants
 * to do something with the code that the module provides.
 */
module_init(tc956x_init_module);

/*!
 * \brief Macro to register the driver un-registration function.
 *
 * \details All modules end by calling either cleanup_module or the function
 * you specify with the module_exit call. This is the exit function for modules;
 * it undoes whatever entry function did. It unregisters the functionality
 * that the entry function registered.
 */
module_exit(tc956x_exit_module);

#ifdef TC956X_PCIE_GEN3_SETTING
module_param(tc956x_speed, uint, 0444);
MODULE_PARM_DESC(tc956x_speed,
		 "PCIe speed Gen TC956X - default is 3, [1..3]");
#endif

module_param(tc956x_port0_interface, uint, 0444);
MODULE_PARM_DESC(tc956x_port0_interface,
		 "PORT0 interface mode TC956X - default is 1,\
		 [0: USXGMII, 1: XFI, 2: RGMII(not supported), 3: SGMII]");

module_param(tc956x_port1_interface, uint, 0444);
MODULE_PARM_DESC(tc956x_port1_interface,
		 "PORT1 interface mode TC956X - default is 3,\
		 [0: USXGMII(not supported), 1: XFI(not supported), 2: RGMII, 3: SGMII]");

MODULE_DESCRIPTION("TC956X PCI Express Ethernet Network Driver");
MODULE_AUTHOR("Toshiba Electronic Devices & Storage Corporation");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_MODULE_VERSION);
