/*
 * TC956X ethernet driver.
 *
 * tc956xmac.h
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
 *  15 Jul 2021 : 1. USXGMII/XFI/SGMII/RGMII interface supported without module parameter
 *  VERSION     : 01-00-02
 *  20 Jul 2021 : 1. Version update
 *		2. Default Port1 interface selected as SGMII
 *  VERSION     : 01-00-03
 *  22 Jul 2021 : 1. Version update
 *		2. USXGMII/XFI/SGMII/RGMII interface supported with module parameters
 *  VERSION     : 01-00-04
 *  22 Jul 2021 : 1. Dynamic CM3 TAMAP configuration
 *  VERSION     : 01-00-05
 *  23 Jul 2021 : 1. Add support for contiguous allocation of memory
 *  VERSION     : 01-00-06
 *  29 Jul 2021 : 1. Add support to set MAC Address register
 *  VERSION     : 01-00-07
 *  05 Aug 2021 : 1. Store and use Port0 pci_dev for all DMA allocation/mapping for IPA path
 *		: 2. Register Port0 as only PCIe device, incase its PHY is not found
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
 *  14 Sep 2021 : 1. Version update
 *  VERSION     : 01-00-13
 *  23 Sep 2021 : 1. Version update
 *  VERSION     : 01-00-14
 *  29 Sep 2021 : 1. Version update
 *  VERSION     : 01-00-15
 *  14 Oct 2021 : 1. Version update
 *  VERSION     : 01-00-16
 *  19 Oct 2021 : 1. Adding M3 SRAM Debug counters to ethtool statistics
 *		: 2. Adding MTL RX Overflow/packet miss count, TX underflow counts,Rx Watchdog value to ethtool statistics.
 *		: 3. Version update
 *  VERSION     : 01-00-17
 *  21 Oct 2021 : 1. Added support for GPIO configuration API
 *		: 2. Version update 
 *  VERSION     : 01-00-18
 *  26 Oct 2021 : 1. Updated Driver Module Version.
		: 2. Added variable for port-wise suspend status.
		: 3. Added macro to control EEE MAC Control.
 *  VERSION     : 01-00-19
 *  04 Nov 2021 : 1. Version update
 *  VERSION     : 01-00-20
 *  08 Nov 2021 : 1. Version update
 *  VERSION     : 01-00-21
 *  24 Nov 2021 : 1. Version update
 		  2. Private member used instead of global for wol interrupt indication
 *  VERSION     : 01-00-22
 *  24 Nov 2021 : 1. Version update
 *  VERSION     : 01-00-23
 *  24 Nov 2021 : 1. EEE macro enabled by default.
 		  2. Module param support for EEE configuration
		  3. Version update
 *  VERSION     : 01-00-24
 *  30 Nov 2021 : 1. Version update
 *  VERSION     : 01-00-25
 *  30 Nov 2021 : 1. Version update
 *  VERSION     : 01-00-26
 *  01 Dec 2021 : 1. Version update
 *  VERSION     : 01-00-27
 *  01 Dec 2021 : 1. Version update
 *  VERSION     : 01-00-28
 *  03 Dec 2021 : 1. Version update
 *  VERSION     : 01-00-29
 *  08 Dec 2021 : 1. Version update
 *  VERSION     : 01-00-30
 *  10 Dec 2021 : 1. Version update
 *  VERSION     : 01-00-31
 *  27 Dec 2021 : 1. Support for eMAC Reset and unused clock disable during Suspend and restoring it back during resume.
		  2. Version update.
 *  VERSION     : 01-00-32
 */

#ifndef __TC956XMAC_H__
#define __TC956XMAC_H__


#include <linux/clk.h>
#include <linux/if_vlan.h>
#include "tc956xmac_inc.h"
#include <linux/phylink.h>
#include <linux/pci.h>
#include "common.h"
#include <linux/ptp_clock_kernel.h>
#include <linux/net_tstamp.h>
#include <linux/reset.h>
#include <net/page_pool.h>
#include <linux/version.h>

//#define TC956X_LOAD_FW_HEADER
#define PF_DRIVER 4

/* Uncomment EEE_MAC_CONTROLLED_MODE macro for MAC controlled EEE Mode & comment for PHY controlled EEE mode */
#define EEE_MAC_CONTROLLED_MODE
/* Uncomment TC956X_5_G_2_5_G_EEE_SUPPORT macro for enabling EEE support for 5G and 2.5G */
#define TC956X_5_G_2_5_G_EEE_SUPPORT
// #define CONFIG_TC956XMAC_SELFTESTS  /*Enable this macro to test Feature selftest*/

#ifdef TC956X
#define VENDOR_ID 0x1179
#define DEVICE_ID 0x0220	/* PF - 0x0220, VF - 0x0221 */
#endif

#define SUB_SYS_VENDOR_ID 0x1179
#define SUB_SYS_DEVICE_ID 0x0001

#define PCI_ETHC_CLASS_CODE	0x020000
#define PCI_ETHC_CLASS_MASK	0xFFFFFF

#define MOD_PARAM_ACCESS 0444

#define TC956X_PCI_DMA_WIDTH 64

#define TC956X_TX_QUEUES 8
#define TC956X_RX_QUEUES 8

#ifndef FIRMWARE_NAME
#define FIRMWARE_NAME "TC956X_Firmware_PCIeBridge.bin"
#endif

#ifdef TC956X

#define TC956X_RESOURCE_NAME	"tc956x_pci-eth"
#define DRV_MODULE_VERSION	"V_01-00-32"
#define TC956X_FW_MAX_SIZE	(64*1024)

#define ATR_AXI4_SLV_BASE		0x0800
#define ATR_AXI4_SLAVE_OFFSET		0x0100
#define ATR_AXI4_TABLE_OFFSET		0x20
#define TC956X_AXI4_SLV(ch, tid)	(ATR_AXI4_SLV_BASE +\
					(ch * ATR_AXI4_SLAVE_OFFSET) +\
					(tid * ATR_AXI4_TABLE_OFFSET))

#define SRC_ADDR_LO_OFFSET		0x00
#define SRC_ADDR_HI_OFFSET		0x04
#define TRSL_ADDR_LO_OFFSET		0x08
#define TRSL_ADDR_HI_OFFSET		0x0C
#define TRSL_PARAM_OFFSET		0x10
#define TRSL_MASK_OFFSET1		0x18
#define TRSL_MASK_OFFSET2		0x1C
#define TC956X_AXI4_SLV_SRC_ADDR_LO(ch, tid)	(TC956X_AXI4_SLV(ch, tid) +\
							SRC_ADDR_LO_OFFSET)
#define TC956X_AXI4_SLV_SRC_ADDR_HI(ch, tid)	(TC956X_AXI4_SLV(ch, tid) +\
							SRC_ADDR_HI_OFFSET)
#define TC956X_AXI4_SLV_TRSL_ADDR_LO(ch, tid)	(TC956X_AXI4_SLV(ch, tid) +\
							TRSL_ADDR_LO_OFFSET)
#define TC956X_AXI4_SLV_TRSL_ADDR_HI(ch, tid)	(TC956X_AXI4_SLV(ch, tid) +\
							TRSL_ADDR_HI_OFFSET)
#define TC956X_AXI4_SLV_TRSL_PARAM(ch, tid)	(TC956X_AXI4_SLV(ch, tid) +\
							TRSL_PARAM_OFFSET)
#define TC956X_AXI4_SLV_TRSL_MASK1(ch, tid)	(TC956X_AXI4_SLV(ch, tid) +\
							TRSL_MASK_OFFSET1)
#define TC956X_AXI4_SLV_TRSL_MASK2(ch, tid)	(TC956X_AXI4_SLV(ch, tid) +\
							TRSL_MASK_OFFSET2)

#define TC956X_ATR_IMPL 1U
#define TC956X_ATR_SIZE(size)		((size - 1U) << 1U)
#define TC956X_ATR_SIZE_MASK		GENMASK(6, 1)
#define TC956x_ATR_SIZE_SHIFT		1
#define TC956X_SRC_LO_MASK		GENMASK(31, 12)
#define TC956X_SRC_LO_SHIFT		12

#define TC956X_AXI4_SLV00_ATR_SIZE 36U
#define TC956X_AXI4_SLV00_SRC_ADDR_LO_VAL  (0x00000000U)
#define TC956X_AXI4_SLV00_SRC_ADDR_HI_VAL  (0x00000010U)
#define TC956X_AXI4_SLV00_TRSL_ADDR_LO_VAL (0x00000000U)
#define TC956X_AXI4_SLV00_TRSL_ADDR_HI_VAL (0x00000000U)
#define TC956X_AXI4_SLV00_TRSL_PARAM_VAL   (0x00000000U)

#ifdef DMA_OFFLOAD_ENABLE
#define TC956X_AXI4_SLV01_ATR_SIZE	    28U	  /* 28 bit DMA Mask */
#define TC956X_AXI4_SLV01_SRC_ADDR_LO_VAL  (0x60000000U)
#define TC956X_AXI4_SLV01_SRC_ADDR_HI_VAL  (0x00000000U)
#define TC956X_AXI4_SLV01_TRSL_ADDR_LO_VAL (0x00000000U)
#define TC956X_AXI4_SLV01_TRSL_ADDR_HI_VAL (0x00000000U)
#define TC956X_AXI4_SLV01_TRSL_PARAM_VAL   (0x00000000U)
#endif

#define TC956X_M3_SRAM_FW_VER_OFFSET 0x4F900 /* DMEM addrs 0x2000F900 */
/* M3 Debug Counters in SRAM*/
#define TC956X_M3_SRAM_DEBUG_CNTS_OFFSET	0x4F800 /* DMEM addrs 0x2000F800 */

#define DB_CNT_LEN	4	/* Size of each debug counter in bytes */
#define DB_CNT0		0	/* reserved0 */
#define DB_CNT1		1	/* reserved1 */
#define DB_CNT2		2	/* reserved2 */
#define DB_CNT3		3	/* reserved3 */
#define DB_CNT4		4	/* reserved4 */
#define DB_CNT5		5	/* reserved5 */
#define DB_CNT6		6	/* reserved6 */
#define DB_CNT7		7	/* reserved7 */
#define DB_CNT8		8	/* reserved8 */
#define DB_CNT9		9	/* reserved9 */
#define DB_CNT10	10	/* reserved10 */
#define DB_CNT11	11	/* m3 watchdog expiry count*/
#define DB_CNT12	12	/* m3 watchdog monitor value */
#define DB_CNT13	13	/* reserved13 */
#define DB_CNT14	14	/* reserved14 */
#define DB_CNT15	15	/* m3 systick counter lower 32bits  */
#define DB_CNT16	16	/* m3 systick counter upper 32bits */
#define DB_CNT17	17	/* m3 transmission timeout indication for port0 */
#define DB_CNT18	18	/* m3 transmission timeout indication for port1 */
#define DB_CNT19	19	/* reserved19 */

#define NRSTCTRL0_RST_ASRT 0x1
#define NRSTCTRL0_RST_DE_ASRT 0x3

#define TC956X_OFFSET_TAMAP 0x00000010
#define TC956X_MASK_TAMAP 0xFFFFF000
#define TC956X_SHIFT_TAMAP 32
#define TC956X_OFFSET_OW 28
#define TC956X_OFFSET_OW_MAX 53
#define TC956X_HEX_ZERO 0x00000000

#define TC956X_BAR0 0
#define TC956X_BAR2 2
#define TC956X_BAR4 4

#define NMODESTS 0x0004
#define NMODESTS_MODE 0x200
#define NMODESTS_MODE2		0x400
#define NMODESTS_MODE2_SHIFT	10
#define TC956X_PCIE_SETTING_A	0 /* x4x1x1 mode */
#define TC956X_PCIE_SETTING_B	1 /* x2x2x1 mode */

#define TC9563_CFG_NEMACTXCDLY		0x1050U
#define TC9563_CFG_NEMACIOCTL		0x107CU

#define NEMACTXCDLY_DEFAULT		0x00000000U
#define NEMACIOCTL_DEFAULT		0xF300F300
/* Systick count SRAM  address  DMEM addrs 0x2000F83C, Check this value for any change */
#define SYSTCIK_SRAM_OFFSET		0x4F83C

/* Tx Timer count SRAM  address  DMEM addrs 0x2000F844, Check this value for any change */
#define TX_TIMER_SRAM_OFFSET_0		0x4F844

/* Tx Timer count SRAM  address  DMEM addrs 0x2000F848, Check this value for any change */
#define TX_TIMER_SRAM_OFFSET_1		0x4F848

#define TX_TIMER_SRAM_OFFSET(t) (((t) == RM_PF0_ID) ? (TX_TIMER_SRAM_OFFSET_0) : (TX_TIMER_SRAM_OFFSET_1))

#define TC956X_M3_SRAM_EEPROM_MAC_ADDR		0x47000		/* DMEM addrs 0x20007000U */
#define TC956X_M3_SRAM_EEPROM_OFFSET_ADDR	0x47050		/* DMEM addrs 0x20007050U */
#define TC956X_M3_SRAM_EEPROM_MAC_COUNT		0x47051		/* DMEM addrs 0x20007051U */
#define TC956X_M3_INIT_DONE					0x47054		/* DMEM addrs 0x20007054U */
#define TC956X_M3_FW_EXIT					0x47058		/* DMEM addrs 0x20007058U */

#define TC956X_M3_DBG_VER_START			0x4F900

#define ENABLE_USXGMII_INTERFACE	0
#define ENABLE_XFI_INTERFACE		1 /* XFI/SFI, this is same as USXGMII, except XPCS autoneg disabled */
#define ENABLE_RGMII_INTERFACE		2
#define ENABLE_SGMII_INTERFACE		3

#define MTL_FPE_AFSZ_64		0
#define MTL_FPE_AFSZ_128	1
#define MTL_FPE_AFSZ_192	2
#define MTL_FPE_AFSZ_256	3
#endif

#define MAX_CM3_TAMAP_ENTRIES		3
#define CM3_TAMAP_ATR_SIZE		28 /* ATR Size = 2 ^ (28 + 1) = 512MB */
#define CM3_TAMAP_SIZE			(1 << (CM3_TAMAP_ATR_SIZE + 1))
#define CM3_TAMAP_MASK			(CM3_TAMAP_SIZE - 1)
#define CM3_TAMAP_SRC_ADDR_START	0x60000000

#define	TC956XMAC_ALIGN(x)		ALIGN(ALIGN(x, SMP_CACHE_BYTES), 16)

#ifdef DMA_OFFLOAD_ENABLE
struct tc956xmac_cm3_tamap {
	u32 trsl_addr_hi;
	u32 trsl_addr_low;
	u32 src_addr_hi;
	u32 src_addr_low;	/* Only [31:12] bits will be considered */
	u32 atr_size;
	bool valid;
};
#endif

struct tc956xmac_resources {

	void __iomem *addr;
#ifdef TC956X
	void __iomem *tc956x_BRIDGE_CFG_pci_base_addr;
	void __iomem *tc956x_SRAM_pci_base_addr;
	void __iomem *tc956x_SFR_pci_base_addr;
#endif
	const char *mac;
	int wol_irq;
	int lpi_irq;
	int irq;
#ifdef TC956X
	unsigned int port_num;
	unsigned int port_interface; /* Kernel module parameter variable for interface */
	unsigned int eee_enabled; /* Parameter to store kernel module parameter to enable/disable EEE */
	unsigned int tx_lpi_timer; /* Parameter to store kernel module parameter for LPI Auto Entry Timer */
#endif
};

struct tc956xmac_tx_info {
	dma_addr_t buf;
	bool map_as_page;
	unsigned int len;
	bool last_segment;
	bool is_jumbo;
};

#define TC956XMAC_TBS_AVAIL	BIT(0)
#define TC956XMAC_TBS_EN		BIT(1)


/* Frequently used values are kept adjacent for cache effect */
struct tc956xmac_tx_queue {
	u32 tx_count_frames;
	int tbs;
	struct timer_list txtimer;
	u32 queue_index;
	struct tc956xmac_priv *priv_data;
	struct dma_extended_desc *dma_etx ____cacheline_aligned_in_smp;
	struct dma_edesc *dma_entx;
	struct dma_desc *dma_tx;
	struct sk_buff **tx_skbuff;
	struct tc956xmac_tx_info *tx_skbuff_dma;
	unsigned int cur_tx;
	unsigned int dirty_tx;
	dma_addr_t dma_tx_phy;
	u32 tx_tail_addr;
	u32 mss;
#ifdef DMA_OFFLOAD_ENABLE
	struct sk_buff **tx_offload_skbuff;
	dma_addr_t *tx_offload_skbuff_dma;
	dma_addr_t buff_tx_phy;
	void *buffer_tx_va_addr;
#endif
};

struct tc956xmac_rx_buffer {
	struct page *page;
	struct page *sec_page;
	dma_addr_t addr;
	dma_addr_t sec_addr;
};

struct tc956xmac_rx_queue {
	u32 rx_count_frames;
	u32 queue_index;
	struct page_pool *page_pool;
	struct tc956xmac_rx_buffer *buf_pool;
	struct tc956xmac_priv *priv_data;
	struct dma_extended_desc *dma_erx;
	struct dma_desc *dma_rx ____cacheline_aligned_in_smp;
	unsigned int cur_rx;
	unsigned int dirty_rx;
	u32 rx_zeroc_thresh;
	dma_addr_t dma_rx_phy;
	u32 rx_tail_addr;
	unsigned int state_saved;
	struct {
		struct sk_buff *skb;
		unsigned int len;
		unsigned int error;
	} state;
#ifdef DMA_OFFLOAD_ENABLE
	struct sk_buff **rx_offload_skbuff;
	dma_addr_t *rx_offload_skbuff_dma;
	dma_addr_t buff_rx_phy;
	void *buffer_rx_va_addr;
#endif
};

struct tc956xmac_channel {
	struct napi_struct rx_napi ____cacheline_aligned_in_smp;
	struct napi_struct tx_napi ____cacheline_aligned_in_smp;
	struct tc956xmac_priv *priv_data;
	spinlock_t lock;
	u32 index;
};

struct tc956xmac_tc_entry {
	bool in_use;
	bool in_hw;
	bool is_last;
	bool is_frag;
	void *frag_ptr;
	unsigned int table_pos;
	u32 handle;
	u32 prio;
	struct {
		u32 match_data;
		u32 match_en;
		u8 af:1;
		u8 rf:1;
		u8 im:1;
		u8 nc:1;
		u8 res1:4;
		u8 frame_offset;
		u8 ok_index;
		u8 dma_ch_no;
		u32 res2;
	} __packed val;
};

#ifdef TC956X
#define TC956XMAC_PPS_MAX		3 /* Two are for output signal generation and one is internal use for eMAC */
#else
#define TC956XMAC_PPS_MAX		4
#endif
struct tc956xmac_pps_cfg {
	bool available;
	struct timespec64 start;
	struct timespec64 period;
};

struct tc956xmac_rss {
	int enable;
	u8 key[TC956XMAC_RSS_HASH_KEY_SIZE];
	u32 table[TC956XMAC_RSS_MAX_TABLE_SIZE];
};

#define TC956XMAC_FLOW_ACTION_DROP		BIT(0)
struct tc956xmac_flow_entry {
	unsigned long cookie;
	unsigned long action;
	u8 ip_proto;
	int in_use;
	int idx;
	int is_l4;
};

struct tc956x_cbs_params {
	u32 send_slope;
	u32 idle_slope;
	u32 high_credit;
	u32 low_credit;
	u32 percentage;
};

struct tc956xmac_priv {
	/* Frequently used values are kept adjacent for cache effect */
	u32 tx_coal_frames;
	u32 tx_coal_timer;
	u32 rx_coal_frames;

	int tx_coalesce;
	int hwts_tx_en;
	bool tx_path_in_lpi_mode;
	bool tso;
	int sph;
	u32 sarc_type;

	unsigned int dma_buf_sz;
	unsigned int rx_copybreak;
	u32 rx_riwt;
	int hwts_rx_en;

	void __iomem *ioaddr;
#ifdef TC956X
	void __iomem *tc956x_BRIDGE_CFG_pci_base_addr;
	void __iomem *tc956x_SRAM_pci_base_addr;
	void __iomem *tc956x_SFR_pci_base_addr;
#endif
	struct net_device *dev;
	struct device *device;
	struct mac_device_info *hw;
	int (*hwif_quirks)(struct tc956xmac_priv *priv);
	struct mutex lock;

	/* RX Queue */
	struct tc956xmac_rx_queue rx_queue[MTL_MAX_RX_QUEUES];

	/* TX Queue */
	struct tc956xmac_tx_queue tx_queue[MTL_MAX_TX_QUEUES];

	/* Generic channel for NAPI */
	struct tc956xmac_channel channel[TC956XMAC_CH_MAX];

	unsigned int l2_filtering_mode; /* 0 - if perfect and 1 - if hash filtering */
	unsigned int vlan_hash_filtering;
	struct tc956x_mac_addr *mac_table;
	struct tc956x_vlan_id *vlan_table;
	u32 sa_vlan_ins_via_reg;
	unsigned char ins_mac_addr[ETH_ALEN];

	/* RX Parser */
	bool rxp_enabled;

	/* PHY state */
	int speed;
	bool oldlink;
	int oldduplex;

	unsigned int flow_ctrl;
	unsigned int pause;
	struct mii_bus *mii;
	int mii_irq[PHY_MAX_ADDR];

	struct phylink_config phylink_config;
	struct phylink *phylink;

	struct tc956xmac_extra_stats xstats ____cacheline_aligned_in_smp;
	struct tc956xmac_safety_stats sstats;
	struct plat_tc956xmacenet_data *plat;
	struct dma_features dma_cap;
	struct tc956xmac_counters mmc;
	int hw_cap_support;
	int synopsys_id;
	u32 msg_enable;
	int wolopts;
	int wol_irq;
	int clk_csr;
	int lpi_irq;
	unsigned int eee_enabled;
	int eee_active;
	unsigned int tx_lpi_timer;
	unsigned int mode;
	unsigned int chain_mode;
	int extend_desc;
	struct hwtstamp_config tstamp_config;
	struct ptp_clock *ptp_clock;
	struct ptp_clock_info ptp_clock_ops;
	unsigned int default_addend;
	u32 sub_second_inc;
	u32 systime_flags;
	u32 adv_ts;
	int use_riwt;
	int irq_wake;
	spinlock_t ptp_lock;
	void __iomem *mmcaddr;
	void __iomem *ptpaddr;
#ifdef TC956X
	void __iomem *xpcsaddr;
	void __iomem *pmaaddr;
#endif
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];

#ifdef CONFIG_DEBUG_FS
	struct dentry *dbgfs_dir;
#endif

	unsigned long state;
	struct workqueue_struct *wq;
	struct work_struct service_task;

	/* CBS configurations */
	struct tc956x_cbs_params cbs_speed100_cfg[8];
	struct tc956x_cbs_params cbs_speed1000_cfg[8];
	struct tc956x_cbs_params cbs_speed2500_cfg[8];
	struct tc956x_cbs_params cbs_speed5000_cfg[8];
	struct tc956x_cbs_params cbs_speed10000_cfg[8];
	/* TC Handling */
	unsigned int tc_entries_max;
	unsigned int tc_off_max;
	struct tc956xmac_tc_entry *tc_entries;
	unsigned int flow_entries_max;
	struct tc956xmac_flow_entry *flow_entries;

	/* Pulse Per Second output */
	struct tc956xmac_pps_cfg pps[TC956XMAC_PPS_MAX];

	/* Receive Side Scaling */
	struct tc956xmac_rss rss;

	/* Tx Checksum Insertion */
	u32 csum_insertion;

	/* CRC Tx Rx Configuraion */
	u32 tx_crc_pad_state;
	u32 rx_crc_pad_state;

	/* eMAC port number */
#ifdef TC956X
	u32 port_num;
	u32 mac_loopback_mode;
	u32 phy_loopback_mode;
	bool is_sgmii_2p5g; /* For 2.5G SGMI, XPCS doesn't support AN. This flag is to identify 2.5G Speed for SGMII interface. */
	u32 port_interface; /* Kernel module parameter variable for interface */
	bool tc956x_port_pm_suspend; /* Port Suspend Status; True : port suspended, False : port resume */
	bool tc956xmac_pm_wol_interrupt; /* Port-wise flag for clearing interrupt after resume. */
#endif

	/* set to 1 when ptp offload is enabled, else 0. */
	u32 ptp_offload;
	/*
	 *ptp offloading mode - ORDINARY_SLAVE, ORDINARY_MASTER,
	 * TRANSPARENT_SLAVE, TRANSPARENT_MASTER, PTOP_TRANSPERENT.
	 */
	u32 ptp_offloading_mode;

	/* set to 1 when onestep timestamp is enabled, else 0. */
	u32 ost_en;

	/* Private data store for platform layer */
	void *plat_priv;
#ifdef DMA_OFFLOAD_ENABLE
	void *client_priv;
	struct tc956xmac_cm3_tamap cm3_tamap[MAX_CM3_TAMAP_ENTRIES];
#endif
	/* Work struct for handling phy interrupt */
	struct work_struct emac_phy_work;
	u32 pm_saved_emac_rst; /* Save and restore EMAC Resets during suspend-resume sequence */
	u32 pm_saved_emac_clk; /* Save and restore EMAC Clocks during suspend-resume sequence */

};

struct tc956x_version {
	unsigned char rel_dbg; /* 'R' for release, 'D' for debug */
	unsigned char major;
	unsigned char minor;
	unsigned char sub_minor;
	unsigned char patch_rel_major;
	unsigned char patch_rel_minor;
};

enum tc956xmac_state {
	TC956XMAC_DOWN,
	TC956XMAC_RESET_REQUESTED,
	TC956XMAC_RESETING,
	TC956XMAC_SERVICE_SCHED,
};

/* for PTP offloading configuration */
#define TC956X_PTP_OFFLOADING_DISABLE			0
#define TC956X_PTP_OFFLOADING_ENABLE			1

#define TC956X_PTP_ORDINARY_SLAVE			1
#define TC956X_PTP_ORDINARY_MASTER			2
#define TC956X_PTP_TRASPARENT_SLAVE			3
#define TC956X_PTP_TRASPARENT_MASTER			4
#define TC956X_PTP_PEER_TO_PEER_TRANSPARENT		5

#define TC956X_AUX_SNAPSHOT_0				1
#define TC956X_AUX_SNAPSHOT_1				2
#define TC956X_AUX_SNAPSHOT_2				4
#define TC956X_AUX_SNAPSHOT_3				8


struct tc956x_ioctl_aux_snapshot {
	__u32 cmd;
	__u32 aux_snapshot_ctrl;
};

struct tc956x_config_ptpoffloading {
	__u32 cmd;
	int en_dis;
	int mode;
	int domain_num;
	int mc_uc;
	unsigned char mc_uc_addr[ETH_ALEN];
};

struct tc956x_config_ost {
	__u32 cmd;
	int en_dis;
};

int tc956xmac_mdio_unregister(struct net_device *ndev);
int tc956xmac_mdio_register(struct net_device *ndev);
int tc956xmac_mdio_reset(struct mii_bus *mii);
void tc956xmac_set_ethtool_ops(struct net_device *netdev);

void tc956xmac_ptp_register(struct tc956xmac_priv *priv);
void tc956xmac_ptp_unregister(struct tc956xmac_priv *priv);
int tc956xmac_resume(struct device *dev);
int tc956xmac_suspend(struct device *dev);
int tc956xmac_dvr_remove(struct device *dev);
int tc956xmac_dvr_probe(struct device *device,
		     struct plat_tc956xmacenet_data *plat_dat,
		     struct tc956xmac_resources *res);
void tc956xmac_disable_eee_mode(struct tc956xmac_priv *priv);
bool tc956xmac_eee_init(struct tc956xmac_priv *priv);

#ifdef CONFIG_TC956XMAC_SELFTESTS
void tc956xmac_selftest_run(struct net_device *dev,
			 struct ethtool_test *etest, u64 *buf);
void tc956xmac_selftest_get_strings(struct tc956xmac_priv *priv, u8 *data);
int tc956xmac_selftest_get_count(struct tc956xmac_priv *priv);
#else
static inline void tc956xmac_selftest_run(struct net_device *dev,
				       struct ethtool_test *etest, u64 *buf)
{
	/* Not enabled */
}
static inline void tc956xmac_selftest_get_strings(struct tc956xmac_priv *priv,
					       u8 *data)
{
	/* Not enabled */
}
static inline int tc956xmac_selftest_get_count(struct tc956xmac_priv *priv)
{
	return -EOPNOTSUPP;
}
#endif /* CONFIG_TC956XMAC_SELFTESTS */

/* Function Prototypes */

s32 tc956x_load_firmware(struct device *dev, struct tc956xmac_resources *res);

int tc956x_set_pci_speed(struct pci_dev *pdev, u32 speed);

#ifdef CONFIG_TC956X_PLATFORM_SUPPORT
int tc956x_platform_probe(struct tc956xmac_priv *priv, struct tc956xmac_resources *res);
int tc956x_platform_remove(struct tc956xmac_priv *priv);
int tc956x_platform_suspend(struct tc956xmac_priv *priv);
int tc956x_platform_resume(struct tc956xmac_priv *priv);
#else
static inline int tc956x_platform_probe(struct tc956xmac_priv *priv, struct tc956xmac_resources *res) { return 0; }
static inline int tc956x_platform_remove(struct tc956xmac_priv *priv) { return 0; }
static inline int tc956x_platform_suspend(struct tc956xmac_priv *priv) { return 0; }
static inline int tc956x_platform_resume(struct tc956xmac_priv *priv) { return 0; }
#endif

int tc956x_GPIO_OutputConfigPin(struct tc956xmac_priv *priv, u32 gpio_pin, u8 out_value);

#endif /* __TC956XMAC_H__ */
