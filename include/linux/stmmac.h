/* SPDX-License-Identifier: GPL-2.0-only */
/*******************************************************************************

  Header file for stmmac platform data

  Copyright (C) 2009  STMicroelectronics Ltd


  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#ifndef __STMMAC_PLATFORM_DATA
#define __STMMAC_PLATFORM_DATA

#include <linux/platform_device.h>
#include <linux/phy.h>

#define MTL_MAX_RX_QUEUES	8
#define MTL_MAX_TX_QUEUES	8
#define STMMAC_CH_MAX		8

#define STMMAC_RX_COE_NONE	0
#define STMMAC_RX_COE_TYPE1	1
#define STMMAC_RX_COE_TYPE2	2

/* Define the macros for CSR clock range parameters to be passed by
 * platform code.
 * This could also be configured at run time using CPU freq framework. */

/* MDC Clock Selection define*/
#define	STMMAC_CSR_60_100M	0x0	/* MDC = clk_scr_i/42 */
#define	STMMAC_CSR_100_150M	0x1	/* MDC = clk_scr_i/62 */
#define	STMMAC_CSR_20_35M	0x2	/* MDC = clk_scr_i/16 */
#define	STMMAC_CSR_35_60M	0x3	/* MDC = clk_scr_i/26 */
#define	STMMAC_CSR_150_250M	0x4	/* MDC = clk_scr_i/102 */
#define	STMMAC_CSR_250_300M	0x5	/* MDC = clk_scr_i/122 */

/* MTL algorithms identifiers */
#define MTL_TX_ALGORITHM_WRR	0x0
#define MTL_TX_ALGORITHM_WFQ	0x1
#define MTL_TX_ALGORITHM_DWRR	0x2
#define MTL_TX_ALGORITHM_SP	0x3
#define MTL_RX_ALGORITHM_SP	0x4
#define MTL_RX_ALGORITHM_WSP	0x5

/* RX/TX Queue Mode */
#define MTL_QUEUE_AVB		0x0
#define MTL_QUEUE_DCB		0x1

/* The MDC clock could be set higher than the IEEE 802.3
 * specified frequency limit 0f 2.5 MHz, by programming a clock divider
 * of value different than the above defined values. The resultant MDIO
 * clock frequency of 12.5 MHz is applicable for the interfacing chips
 * supporting higher MDC clocks.
 * The MDC clock selection macros need to be defined for MDC clock rate
 * of 12.5 MHz, corresponding to the following selection.
 */
#define STMMAC_CSR_I_4		0x8	/* clk_csr_i/4 */
#define STMMAC_CSR_I_6		0x9	/* clk_csr_i/6 */
#define STMMAC_CSR_I_8		0xA	/* clk_csr_i/8 */
#define STMMAC_CSR_I_10		0xB	/* clk_csr_i/10 */
#define STMMAC_CSR_I_12		0xC	/* clk_csr_i/12 */
#define STMMAC_CSR_I_14		0xD	/* clk_csr_i/14 */
#define STMMAC_CSR_I_16		0xE	/* clk_csr_i/16 */
#define STMMAC_CSR_I_18		0xF	/* clk_csr_i/18 */

/* AXI DMA Burst length supported */
#define DMA_AXI_BLEN_4		(1 << 1)
#define DMA_AXI_BLEN_8		(1 << 2)
#define DMA_AXI_BLEN_16		(1 << 3)
#define DMA_AXI_BLEN_32		(1 << 4)
#define DMA_AXI_BLEN_64		(1 << 5)
#define DMA_AXI_BLEN_128	(1 << 6)
#define DMA_AXI_BLEN_256	(1 << 7)
#define DMA_AXI_BLEN_ALL (DMA_AXI_BLEN_4 | DMA_AXI_BLEN_8 | DMA_AXI_BLEN_16 \
			| DMA_AXI_BLEN_32 | DMA_AXI_BLEN_64 \
			| DMA_AXI_BLEN_128 | DMA_AXI_BLEN_256)

struct stmmac_priv;

/* Platfrom data for platform device structure's platform_data field */

struct stmmac_mdio_bus_data {
	unsigned int phy_mask;
	unsigned int has_xpcs;
	unsigned int xpcs_an_inband;
	int *irqs;
	int probed_phy_irq;
	bool needs_reset;
};

struct stmmac_dma_cfg {
	int pbl;
	int txpbl;
	int rxpbl;
	bool pblx8;
	int fixed_burst;
	int mixed_burst;
	bool aal;
	bool eame;
	bool multi_msi_en;
	bool dche;
};

#define AXI_BLEN	7
struct stmmac_axi {
	bool axi_lpi_en;
	bool axi_xit_frm;
	u32 axi_wr_osr_lmt;
	u32 axi_rd_osr_lmt;
	bool axi_kbbe;
	u32 axi_blen[AXI_BLEN];
	bool axi_fb;
	bool axi_mb;
	bool axi_rb;
};

#define EST_GCL		1024
struct stmmac_est {
	struct mutex lock;
	int enable;
	u32 btr_reserve[2];
	u32 btr_offset[2];
	u32 btr[2];
	u32 ctr[2];
	u32 ter;
	u32 gcl_unaligned[EST_GCL];
	u32 gcl[EST_GCL];
	u32 gcl_size;
};

struct stmmac_rxq_cfg {
	u8 mode_to_use;
	u32 chan;
	u8 pkt_route;
	bool use_prio;
	u32 prio;
};

struct stmmac_txq_cfg {
	u32 weight;
	u8 mode_to_use;
	/* Credit Base Shaper parameters */
	u32 send_slope;
	u32 idle_slope;
	u32 high_credit;
	u32 low_credit;
	bool use_prio;
	u32 prio;
	int tbs_en;
};

/* FPE link state */
enum stmmac_fpe_state {
	FPE_STATE_OFF = 0,
	FPE_STATE_CAPABLE = 1,
	FPE_STATE_ENTERING_ON = 2,
	FPE_STATE_ON = 3,
};

/* FPE link-partner hand-shaking mPacket type */
enum stmmac_mpacket_type {
	MPACKET_VERIFY = 0,
	MPACKET_RESPONSE = 1,
};

enum stmmac_fpe_task_state_t {
	__FPE_REMOVING,
	__FPE_TASK_SCHED,
};

struct stmmac_fpe_cfg {
	bool enable;				/* FPE enable */
	bool hs_enable;				/* FPE handshake enable */
	enum stmmac_fpe_state lp_fpe_state;	/* Link Partner FPE state */
	enum stmmac_fpe_state lo_fpe_state;	/* Local station FPE state */
};

struct stmmac_safety_feature_cfg {
	u32 tsoee;
	u32 mrxpee;
	u32 mestee;
	u32 mrxee;
	u32 mtxee;
	u32 epsi;
	u32 edpp;
	u32 prtyen;
	u32 tmouten;
};

/* Addresses that may be customized by a platform */
struct dwmac4_addrs {
	u32 dma_chan;
	u32 dma_chan_offset;
	u32 mtl_chan;
	u32 mtl_chan_offset;
	u32 mtl_ets_ctrl;
	u32 mtl_ets_ctrl_offset;
	u32 mtl_txq_weight;
	u32 mtl_txq_weight_offset;
	u32 mtl_send_slp_cred;
	u32 mtl_send_slp_cred_offset;
	u32 mtl_high_cred;
	u32 mtl_high_cred_offset;
	u32 mtl_low_cred;
	u32 mtl_low_cred_offset;
};

#define STMMAC_FLAG_HAS_INTEGRATED_PCS		BIT(0)
#define STMMAC_FLAG_SPH_DISABLE			BIT(1)
#define STMMAC_FLAG_USE_PHY_WOL			BIT(2)
#define STMMAC_FLAG_HAS_SUN8I			BIT(3)
#define STMMAC_FLAG_TSO_EN			BIT(4)
#define STMMAC_FLAG_SERDES_UP_AFTER_PHY_LINKUP	BIT(5)
#define STMMAC_FLAG_VLAN_FAIL_Q_EN		BIT(6)
#define STMMAC_FLAG_MULTI_MSI_EN		BIT(7)
#define STMMAC_FLAG_EXT_SNAPSHOT_EN		BIT(8)
#define STMMAC_FLAG_INT_SNAPSHOT_EN		BIT(9)
#define STMMAC_FLAG_RX_CLK_RUNS_IN_LPI		BIT(10)
#define STMMAC_FLAG_EN_TX_LPI_CLOCKGATING	BIT(11)
#define STMMAC_FLAG_HWTSTAMP_CORRECT_LATENCY	BIT(12)

struct plat_stmmacenet_data {
	int bus_id;
	int phy_addr;
	/* MAC ----- optional PCS ----- SerDes ----- optional PHY ----- Media
	 *       ^                               ^
	 * mac_interface                   phy_interface
	 *
	 * mac_interface is the MAC-side interface, which may be the same
	 * as phy_interface if there is no intervening PCS. If there is a
	 * PCS, then mac_interface describes the interface mode between the
	 * MAC and PCS, and phy_interface describes the interface mode
	 * between the PCS and PHY.
	 */
	phy_interface_t mac_interface;
	/* phy_interface is the PHY-side interface - the interface used by
	 * an attached PHY.
	 */
	phy_interface_t phy_interface;
	struct stmmac_mdio_bus_data *mdio_bus_data;
	struct device_node *phy_node;
	struct fwnode_handle *port_node;
	struct device_node *mdio_node;
	struct stmmac_dma_cfg *dma_cfg;
	struct stmmac_est *est;
	struct stmmac_fpe_cfg *fpe_cfg;
	struct stmmac_safety_feature_cfg *safety_feat_cfg;
	int clk_csr;
	int has_gmac;
	int enh_desc;
	int tx_coe;
	int rx_coe;
	int bugged_jumbo;
	int pmt;
	int force_sf_dma_mode;
	int force_thresh_dma_mode;
	int riwt_off;
	int max_speed;
	int maxmtu;
	int multicast_filter_bins;
	int unicast_filter_entries;
	int tx_fifo_size;
	int rx_fifo_size;
	u32 host_dma_width;
	u32 rx_queues_to_use;
	u32 tx_queues_to_use;
	u8 rx_sched_algorithm;
	u8 tx_sched_algorithm;
	struct stmmac_rxq_cfg rx_queues_cfg[MTL_MAX_RX_QUEUES];
	struct stmmac_txq_cfg tx_queues_cfg[MTL_MAX_TX_QUEUES];
	void (*fix_mac_speed)(void *priv, unsigned int speed, unsigned int mode);
	int (*fix_soc_reset)(void *priv, void __iomem *ioaddr);
	int (*serdes_powerup)(struct net_device *ndev, void *priv);
	void (*serdes_powerdown)(struct net_device *ndev, void *priv);
	void (*speed_mode_2500)(struct net_device *ndev, void *priv);
	void (*ptp_clk_freq_config)(struct stmmac_priv *priv);
	int (*init)(struct platform_device *pdev, void *priv);
	void (*exit)(struct platform_device *pdev, void *priv);
	struct mac_device_info *(*setup)(void *priv);
	int (*clks_config)(void *priv, bool enabled);
	int (*crosststamp)(ktime_t *device, struct system_counterval_t *system,
			   void *ctx);
	void (*dump_debug_regs)(void *priv);
	void *bsp_priv;
	struct clk *stmmac_clk;
	struct clk *pclk;
	struct clk *clk_ptp_ref;
	unsigned int clk_ptp_rate;
	unsigned int clk_ref_rate;
	unsigned int mult_fact_100ns;
	s32 ptp_max_adj;
	u32 cdc_error_adj;
	struct reset_control *stmmac_rst;
	struct reset_control *stmmac_ahb_rst;
	struct stmmac_axi *axi;
	int has_gmac4;
	int rss_en;
	int mac_port_sel_speed;
	int has_xgmac;
	u8 vlan_fail_q;
	unsigned int eee_usecs_rate;
	struct pci_dev *pdev;
	int int_snapshot_num;
	int ext_snapshot_num;
	int msi_mac_vec;
	int msi_wol_vec;
	int msi_lpi_vec;
	int msi_sfty_ce_vec;
	int msi_sfty_ue_vec;
	int msi_rx_base_vec;
	int msi_tx_base_vec;
	const struct dwmac4_addrs *dwmac4_addrs;
	unsigned int flags;
};
#endif
