/*
 * TC956X ethernet driver.
 *
 * tc956xmac_main.c
 *
 * Copyright(C) 2007-2011 STMicroelectronics Ltd
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
 *  20 Jul 2021 : 1. Flash MDIO ioctl supported
 *		  2. Rxp statistics function removed
 *  VERSION     : 01-00-03
 *  22 Jul 2021 : 1. USXGMII/XFI/SGMII/RGMII interface supported with module parameters
 *  VERSION     : 01-00-04
 *  22 Jul 2021 : 1. Dynamic CM3 TAMAP configuration
 *  VERSION     : 01-00-05
 *  05 Aug 2021 : 1. Register Port0 as only PCIe device, incase its PHY is not found
 *  VERSION     : 01-00-08
 *  16 Aug 2021 : 1. PHY interrupt mode supported through .config_intr and .ack_interrupt API
 *  VERSION     : 01-00-09
 *  24 Aug 2021 : 1. Platform API supported
 *  VERSION     : 01-00-10
 *  14 Sep 2021 : 1. Synchronization between ethtool vlan features
 *  		  "rx-vlan-offload", "rx-vlan-filter", "tx-vlan-offload" output and register settings.
 * 		  2. Added ethtool support to update "rx-vlan-offload", "rx-vlan-filter",
 *  		  and "tx-vlan-offload".
 * 		  3. Removed IOCTL TC956XMAC_VLAN_STRIP_CONFIG.
 * 		  4. Removed "Disable VLAN Filter" option in IOCTL TC956XMAC_VLAN_FILTERING.
 *  VERSION     : 01-00-13
 *  23 Sep 2021 : 1. Capturing RBU status using MAC EVENT Interupt and updating to ethtool statistics for both S/W & IPA DMA channels
 *  VERSION     : 01-00-14
 *  21 Oct 2021 : 1. Added support for GPIO configuration API
 *  VERSION     : 01-00-18
 *  26 Oct 2021 : 1. Added support for EEE PHY and MAC Control Mode.
		  2. Added dev_pm_ops interface support for suspend-resume.
		  3. Changed IRQF_SHARED to IRQF_NO_SUSPEND and added WOL Interrupt Handler support.
		  4. Added Platform Apis.
 *  VERSION     : 01-00-19
 *  04 Nov 2021 : 1. Stopped disabling/enabling of MAC TX during Link down/up.
 *  VERSION     : 01-00-20
 *  08 Nov 2021 : 1. Skip queuing PHY Work during suspend.
 *  VERSION     : 01-00-21
 *  24 Nov 2021 : 1. Private member used instead of global for wol interrupt indication
 *  VERSION     : 01-00-22
 *  24 Nov 2021 : 1. EEE update for runtime configuration and LPI interrupt disabled.
 		  2. EEE SW timers removed. Only HW timers used to control EEE LPI entry/exit
 		  3. USXGMII support during link change
 *  VERSION     : 01-00-24
 *  30 Nov 2021 : 1. Added PHY Workqueue Cancel during suspend only if network interface available.
 *  VERSION     : 01-00-26
 *  01 Dec 2021 : 1. Free EMAC IRQ during suspend and request EMAC IRQ during resume.
 *  VERSION     : 01-00-27
 *  03 Dec 2021 : 1. Added error check for phydev in tc956xmac_suspend().
 *  VERSION     : 01-00-29
 *  08 Dec 2021 : 1. Added Module parameter for Rx & Tx Queue Size configuration.
 *  VERSION     : 01-00-30
 *  10 Dec 2021 : 1. Added Module parameter to count Link partner pause frames and output to ethtool.
 *  VERSION     : 01-00-31
 *  27 Dec 2021 : 1. Support for eMAC Reset and unused clock disable during Suspend and restoring it back during resume.
		  2. Resetting and disabling of unused clocks for eMAC Port, when no-found PHY for that particular port.
		  3. Valid phy-address and mii-pointer NULL check in tc956xmac_suspend().
		  4. Version update.
 *  VERSION     : 01-00-32
 *  06 Jan 2022 : 1. Null check added while freeing skb buff data
 *  VERSION     : 01-00-33
 *  07 Jan 2022 : 1. During emac resume, attach the net device after initializing the queues
 *  VERSION     : 01-00-34
*/

#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/skbuff.h>
#include <linux/ethtool.h>
#include <linux/if_ether.h>
#include <linux/crc32.h>
#include <linux/mii.h>
#include <linux/if.h>
#include <linux/if_vlan.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/prefetch.h>
#include <linux/pinctrl/consumer.h>
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#endif /* CONFIG_DEBUG_FS */
#include <linux/net_tstamp.h>
#include <linux/phylink.h>
#include <linux/udp.h>
#include <net/pkt_cls.h>
#include "tc956xmac_ptp.h"
#include "tc956xmac.h"
#include <linux/reset.h>
#include <linux/of_mdio.h>
#include <linux/version.h>
#include <linux/ctype.h>
#include "dwxgmac2.h"
#include "hwif.h"
#include "common.h"
#include "tc956xmac_ioctl.h"
#ifdef TC956X_5_G_2_5_G_EEE_SUPPORT
#include <linux/phy.h>
#include <linux/linkmode.h>
#endif

#ifdef TC956X_PCIE_LOGSTAT
#include "tc956x_pcie_logstat.h"
#endif /* #ifdef TC956X_PCIE_LOGSTAT */

#define	TSO_MAX_BUFF_SIZE	(SZ_16K - 1)
#define PPS_START_DELAY		100000000	/* 100 ms, in unit of ns */

/* Module parameters */
#define TX_TIMEO	5000
static int watchdog = TX_TIMEO;
module_param(watchdog, int, 0644);
MODULE_PARM_DESC(watchdog, "Transmit timeout in milliseconds (default 5s)");

static int debug = -1;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Message Level (-1: default, 0: no output, 16: all)");

static int phyaddr = -1;
module_param(phyaddr, int, 0444);
MODULE_PARM_DESC(phyaddr, "Physical device address");

#define TC956XMAC_TX_THRESH	(DMA_TX_SIZE / 4)
#define TC956XMAC_RX_THRESH	(DMA_RX_SIZE / 4)

static int flow_ctrl = FLOW_AUTO;
module_param(flow_ctrl, int, 0644);
MODULE_PARM_DESC(flow_ctrl, "Flow control ability [on/off]");

static int pause = PAUSE_TIME;
module_param(pause, int, 0644);
MODULE_PARM_DESC(pause, "Flow Control Pause Time");

#define TC_DEFAULT 64
static int tc = TC_DEFAULT;
module_param(tc, int, 0644);
MODULE_PARM_DESC(tc, "DMA threshold control value");

#define	DEFAULT_BUFSIZE	1536
static int buf_sz = DEFAULT_BUFSIZE;
module_param(buf_sz, int, 0644);
MODULE_PARM_DESC(buf_sz, "DMA buffer size");

#define	TC956XMAC_RX_COPYBREAK	256

static const u32 default_msg_level = (NETIF_MSG_DRV | NETIF_MSG_PROBE |
				      NETIF_MSG_LINK | NETIF_MSG_IFUP |
				      NETIF_MSG_IFDOWN | NETIF_MSG_TIMER);

static void tc956x_ptp_configuration(struct tc956xmac_priv *priv, u32 tcr_config);

#define TC956XMAC_DEFAULT_LPI_TIMER	1000
static int eee_timer = TC956XMAC_DEFAULT_LPI_TIMER;
module_param(eee_timer, int, 0644);
MODULE_PARM_DESC(eee_timer, "LPI tx expiration time in msec");
#define TC956XMAC_LPI_T(x) (jiffies + msecs_to_jiffies(x))

/* By default the driver will use the ring mode to manage tx and rx descriptors,
 * but allow user to force to use the chain instead of the ring
 */
static unsigned int chain_mode;
module_param(chain_mode, int, 0444);
MODULE_PARM_DESC(chain_mode, "To use chain instead of ring mode");

static irqreturn_t tc956xmac_interrupt(int irq, void *dev_id);

#ifdef CONFIG_DEBUG_FS
static const struct net_device_ops tc956xmac_netdev_ops;
static void tc956xmac_init_fs(struct net_device *dev);
static void tc956xmac_exit_fs(struct net_device *dev);
#endif

#define TC956XMAC_COAL_TIMER(x) (jiffies + usecs_to_jiffies(x))

/* MAC address */

static u8 dev_addr[2][6] = {{0xEC, 0x21, 0xE5, 0x10, 0x4F, 0xEA},
			{0xEC, 0x21, 0xE5, 0x11, 0x4F, 0xEA}};
#ifdef TC956X
#define TX_PRESET_MAX		11
static u8 tx_demphasis_setting[TX_PRESET_MAX][2] =	{
			{0x00, 0x14}, /*Preshoot: 0 dB, De-emphasis: -6 dB*/
			{0x00, 0x0E}, /*Preshoot: 0 dB, De-emphasis: -3.5 dB*/
			{0x00, 0x10}, /*Preshoot: 0 dB, De-emphasis: -4.4 dB*/
			{0x00, 0x0A}, /*Preshoot: 0 dB, De-emphasis: -2.5 dB*/
			{0x00, 0x00}, /*Preshoot: 0 dB, De-emphasis: 0 dB*/
			{0x08, 0x00}, /*Preshoot: 1.9 dB, De-emphasis: 0 dB*/
			{0x0A, 0x00}, /*Preshoot: 2.5 dB, De-emphasis: 0 dB*/
			{0x08, 0x10}, /*Preshoot: 3.5 dB, De-emphasis: -6 dB*/
			{0x0A, 0x0A}, /*Preshoot: 3.5 dB, De-emphasis: -3.5 dB*/
			{0x0E, 0x00}, /*Preshoot: 3.5 dB, De-emphasis: 0 dB*/
			{0x00, 0x1B}, /*Preshoot: 0 dB, De-emphasis: -9.1 dB*/
};
#endif

struct config_parameter_list {
	char mdio_key[32];
	unsigned short mdio_key_len;
	char mac_key[32];
	unsigned short mac_key_len;
	unsigned short mac_str_len;
	char mac_str_def[20];
};

static const struct config_parameter_list config_param_list[] = {
{"MDIOBUSID", 9, "MAC_ID", 6, 17, "00:00:00:00:00:00"},
};

static uint16_t mdio_bus_id;

#define CONFIG_PARAM_NUM ARRAY_SIZE(config_param_list)
int tc956xmac_rx_parser_configuration(struct tc956xmac_priv *);

/* Source Address in Pause frame from PHY */
static u8 phy_sa_addr[2][6] = {
	{0x00, 0x01, 0x02, 0x03, 0x04, 0x05}, /*For Port-0*/
	{0x00, 0x01, 0x02, 0x03, 0x04, 0x05}, /*For Port-1*/
};
extern unsigned int mac0_en_lp_pause_frame_cnt;
extern unsigned int mac1_en_lp_pause_frame_cnt;

/**
 *  tc956x_GPIO_OutputConfigPin - to configure GPIO as output and write the value
 *  @priv: driver private structure
 *  @gpio_pin: GPIO pin number
 *  @out_value : value to write to the GPIO pin. Can be 0 or 1
 *  @remarks : Only GPIO0- GPIO06, GPI010-GPIO12 are allowed
 */
int tc956x_GPIO_OutputConfigPin(struct tc956xmac_priv *priv, u32 gpio_pin, u8 out_value)
{
	u32 config, val;

	/* Only GPIO0- GPIO06, GPI010-GPIO12 are allowed */ 
	switch (gpio_pin) {
		case GPIO_00:
			val = readl(priv->ioaddr + NFUNCEN4_OFFSET);
			val &= ~NFUNCEN4_GPIO_00;
			val |= (NFUNCEN_FUNC0 << NFUNCEN4_GPIO_00_SHIFT);
			writel(val, priv->ioaddr + NFUNCEN4_OFFSET);
			break;
		case GPIO_01:
			val = readl(priv->ioaddr + NFUNCEN4_OFFSET);
			val &= ~NFUNCEN4_GPIO_01;
			val |= (NFUNCEN_FUNC0 << NFUNCEN4_GPIO_01_SHIFT);
			writel(val, priv->ioaddr + NFUNCEN4_OFFSET);
			break;
		case GPIO_02:
			val = readl(priv->ioaddr + NFUNCEN4_OFFSET);
			val &= ~NFUNCEN4_GPIO_02;
			val |= (NFUNCEN_FUNC0 << NFUNCEN4_GPIO_02_SHIFT);
			writel(val, priv->ioaddr + NFUNCEN4_OFFSET);
			break;
		case GPIO_03:
			val = readl(priv->ioaddr + NFUNCEN4_OFFSET);
			val &= ~NFUNCEN4_GPIO_03;
			val |= (NFUNCEN_FUNC0 << NFUNCEN4_GPIO_03_SHIFT);
			writel(val, priv->ioaddr + NFUNCEN4_OFFSET);
			break;
		case GPIO_04:
			val = readl(priv->ioaddr + NFUNCEN4_OFFSET);
			val &= ~NFUNCEN4_GPIO_04;
			val |= (NFUNCEN_FUNC0 << NFUNCEN4_GPIO_04_SHIFT);
			writel(val, priv->ioaddr + NFUNCEN4_OFFSET);
			break;
		case GPIO_05:
			val = readl(priv->ioaddr + NFUNCEN4_OFFSET);
			val &= ~NFUNCEN4_GPIO_05;
			val |= (NFUNCEN_FUNC0 << NFUNCEN4_GPIO_05_SHIFT);
			writel(val, priv->ioaddr + NFUNCEN4_OFFSET);
			break;
		case GPIO_06:
			val = readl(priv->ioaddr + NFUNCEN4_OFFSET);
			val &= ~NFUNCEN4_GPIO_06;
			val |= (NFUNCEN_FUNC0 << NFUNCEN4_GPIO_06_SHIFT);
			writel(val, priv->ioaddr + NFUNCEN4_OFFSET);
			break;
		case GPIO_10:
			val = readl(priv->ioaddr + NFUNCEN5_OFFSET);
			val &= ~NFUNCEN5_GPIO_10;
			val |= (NFUNCEN_FUNC0 << NFUNCEN5_GPIO_10_SHIFT);
			writel(val, priv->ioaddr + NFUNCEN5_OFFSET);
			break;
		case GPIO_11:
			val = readl(priv->ioaddr + NFUNCEN5_OFFSET);
			val &= ~NFUNCEN5_GPIO_11;
			val |= (NFUNCEN_FUNC0 << NFUNCEN5_GPIO_11_SHIFT);
			writel(val, priv->ioaddr + NFUNCEN5_OFFSET);
			break;
		case GPIO_12:
			val = readl(priv->ioaddr + NFUNCEN6_OFFSET);
			val &= ~NFUNCEN6_GPIO_12;
			val |= (NFUNCEN_FUNC0 << NFUNCEN6_GPIO_12_SHIFT);
			writel(val, priv->ioaddr + NFUNCEN6_OFFSET);
			break;
		default : 
			netdev_err(priv->dev, "Invalid GPIO pin - %d\n", gpio_pin);
			return -EPERM;
	}

	/* Write data to GPIO pin */
	if(gpio_pin < GPIO_32) {
		config = 1 << gpio_pin; 
		val = readl(priv->ioaddr + GPIOO0_OFFSET);
		val &= ~config;
		if(out_value)
			val |= config;

		writel(val, priv->ioaddr + GPIOO0_OFFSET);
	}  else {
		config = 1 << (gpio_pin - GPIO_32);
		val = readl(priv->ioaddr + GPIOO1_OFFSET);
		val &= ~config;
		if(out_value)
			val |= config;

		writel(val, priv->ioaddr + GPIOO1_OFFSET);
	}

	/* Configure the GPIO pin in output direction */
	if(gpio_pin < GPIO_32) {
		config = ~(1 << gpio_pin) ;
		val = readl(priv->ioaddr + GPIOE0_OFFSET);
		writel(val & config, priv->ioaddr + GPIOE0_OFFSET);
	} else {
		config = ~(1 << (gpio_pin - GPIO_32)) ;
		val = readl(priv->ioaddr + GPIOE1_OFFSET);
		writel(val & config, priv->ioaddr + GPIOE1_OFFSET);
	}

	return 0;
}

/**
 *  tc956xmac_wol_interrupt - ISR to handle WoL PHY interrupt
 *  @irq: interrupt number.
 *  @dev_id: to pass the net device pointer.
 */
static irqreturn_t tc956xmac_wol_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct tc956xmac_priv *priv = netdev_priv(dev);

	/* Set flag to clear interrupt after resume */
	DBGPR_FUNC(priv->device, "%s\n", __func__);
	/* Set flag to indicate WOL interrupt trigger */
	priv->tc956xmac_pm_wol_interrupt = true;
	return IRQ_HANDLED;
}

/**
 * tc956xmac_verify_args - verify the driver parameters.
 * Description: it checks the driver parameters and set a default in case of
 * errors.
 */
static void tc956xmac_verify_args(void)
{
	if (unlikely(watchdog < 0))
		watchdog = TX_TIMEO;
	if (unlikely((buf_sz < DEFAULT_BUFSIZE) || (buf_sz > BUF_SIZE_16KiB)))
		buf_sz = DEFAULT_BUFSIZE;
	if (unlikely(flow_ctrl > 1))
		flow_ctrl = FLOW_AUTO;
	else if (likely(flow_ctrl < 0))
		flow_ctrl = FLOW_OFF;
	if (unlikely((pause < 0) || (pause > 0xffff)))
		pause = PAUSE_TIME;
	if (eee_timer < 0)
		eee_timer = TC956XMAC_DEFAULT_LPI_TIMER;
}

/**
 * tc956xmac_disable_all_queues - Disable all queues
 * @priv: driver private structure
 */
static void tc956xmac_disable_all_queues(struct tc956xmac_priv *priv)
{
#ifdef TC956X
	u32 rx_queues_cnt = priv->plat->rx_queues_to_use;
#endif

	u32 tx_queues_cnt = priv->plat->tx_queues_to_use;
	u32 maxq = max(rx_queues_cnt, tx_queues_cnt);
	u32 queue;

	for (queue = 0; queue < maxq; queue++) {
		struct tc956xmac_channel *ch = &priv->channel[queue];

		if (queue < rx_queues_cnt && priv->plat->rx_dma_ch_owner[queue] == USE_IN_TC956X_SW)
			napi_disable(&ch->rx_napi);

		if (queue < tx_queues_cnt && priv->plat->tx_dma_ch_owner[queue] == USE_IN_TC956X_SW)
			napi_disable(&ch->tx_napi);
	}
}

/**
 * tc956xmac_enable_all_queues - Enable all queues
 * @priv: driver private structure
 */
static void tc956xmac_enable_all_queues(struct tc956xmac_priv *priv)
{
#ifdef TC956X
	u32 rx_queues_cnt = priv->plat->rx_queues_to_use;
#endif

	u32 tx_queues_cnt = priv->plat->tx_queues_to_use;
	u32 maxq = max(rx_queues_cnt, tx_queues_cnt);
	u32 queue;

	for (queue = 0; queue < maxq; queue++) {
		struct tc956xmac_channel *ch = &priv->channel[queue];

		if (queue < rx_queues_cnt && priv->plat->rx_dma_ch_owner[queue] == USE_IN_TC956X_SW)
			napi_enable(&ch->rx_napi);
		if (queue < tx_queues_cnt && priv->plat->tx_dma_ch_owner[queue] == USE_IN_TC956X_SW)
			napi_enable(&ch->tx_napi);
	}
}

/**
 * tc956xmac_stop_all_queues - Stop all queues
 * @priv: driver private structure
 */
static void tc956xmac_stop_all_queues(struct tc956xmac_priv *priv)
{
	u32 tx_queues_cnt = priv->plat->tx_queues_to_use;
	u32 queue;

	for (queue = 0; queue < tx_queues_cnt; queue++) {
		if (priv->plat->tx_dma_ch_owner[queue] != USE_IN_TC956X_SW)
			continue;

		netif_tx_stop_queue(netdev_get_tx_queue(priv->dev, queue));
	}
}

/**
 * tc956xmac_start_all_queues - Start all queues
 * @priv: driver private structure
 */
static void tc956xmac_start_all_queues(struct tc956xmac_priv *priv)
{
	u32 tx_queues_cnt = priv->plat->tx_queues_to_use;
	u32 queue;

	for (queue = 0; queue < tx_queues_cnt; queue++) {
		if (priv->plat->tx_dma_ch_owner[queue] != USE_IN_TC956X_SW)
			continue;

		netif_tx_start_queue(netdev_get_tx_queue(priv->dev, queue));
	}
}

#ifdef TC956X_UNSUPPORTED_UNTESTED
static void tc956xmac_service_event_schedule(struct tc956xmac_priv *priv)
{
	if (!test_bit(TC956XMAC_DOWN, &priv->state) &&
	    !test_and_set_bit(TC956XMAC_SERVICE_SCHED, &priv->state))
		queue_work(priv->wq, &priv->service_task);
}
#endif

static void tc956xmac_global_err(struct tc956xmac_priv *priv)
{
	netif_carrier_off(priv->dev);
	set_bit(TC956XMAC_RESET_REQUESTED, &priv->state);
#ifdef TC956X_UNSUPPORTED_UNTESTED
	tc956xmac_service_event_schedule(priv);
#endif
}

#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
/**
 * tc956xmac_clk_csr_set - dynamically set the MDC clock
 * @priv: driver private structure
 * Description: this is to dynamically set the MDC clock according to the csr
 * clock input.
 * Note:
 *	If a specific clk_csr value is passed from the platform
 *	this means that the CSR Clock Range selection cannot be
 *	changed at run-time and it is fixed (as reported in the driver
 *	documentation). Viceversa the driver will try to set the MDC
 *	clock dynamically according to the actual clock input.
 */
static void tc956xmac_clk_csr_set(struct tc956xmac_priv *priv)
{
	u32 clk_rate;

	clk_rate = clk_get_rate(priv->plat->tc956xmac_clk);

	/* Platform provided default clk_csr would be assumed valid
	 * for all other cases except for the below mentioned ones.
	 * For values higher than the IEEE 802.3 specified frequency
	 * we can not estimate the proper divider as it is not known
	 * the frequency of clk_csr_i. So we do not change the default
	 * divider.
	 */
	if (!(priv->clk_csr & MAC_CSR_H_FRQ_MASK)) {
		if (clk_rate < CSR_F_35M)
			priv->clk_csr = TC956XMAC_CSR_20_35M;
		else if ((clk_rate >= CSR_F_35M) && (clk_rate < CSR_F_60M))
			priv->clk_csr = TC956XMAC_CSR_35_60M;
		else if ((clk_rate >= CSR_F_60M) && (clk_rate < CSR_F_100M))
			priv->clk_csr = TC956XMAC_CSR_60_100M;
		else if ((clk_rate >= CSR_F_100M) && (clk_rate < CSR_F_150M))
			priv->clk_csr = TC956XMAC_CSR_100_150M;
		else if ((clk_rate >= CSR_F_150M) && (clk_rate < CSR_F_250M))
			priv->clk_csr = TC956XMAC_CSR_150_250M;
		else if ((clk_rate >= CSR_F_250M) && (clk_rate < CSR_F_300M))
			priv->clk_csr = TC956XMAC_CSR_250_300M;
	}

	if (priv->plat->has_sun8i) {
		if (clk_rate > 160000000)
			priv->clk_csr = 0x03;
		else if (clk_rate > 80000000)
			priv->clk_csr = 0x02;
		else if (clk_rate > 40000000)
			priv->clk_csr = 0x01;
		else
			priv->clk_csr = 0;
	}

	if (priv->plat->has_xgmac) {
		if (clk_rate > 400000000)
			priv->clk_csr = 0x5;
		else if (clk_rate > 350000000)
			priv->clk_csr = 0x4;
		else if (clk_rate > 300000000)
			priv->clk_csr = 0x3;
		else if (clk_rate > 250000000)
			priv->clk_csr = 0x2;
		else if (clk_rate > 150000000)
			priv->clk_csr = 0x1;
		else
			priv->clk_csr = 0x0;
	}
}
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */
static void print_pkt(unsigned char *buf, int len)
{
	pr_debug("len = %d byte, buf addr: 0x%p\n", len, buf);
	print_hex_dump_bytes("", DUMP_PREFIX_OFFSET, buf, len);
}

static inline u32 tc956xmac_tx_avail(struct tc956xmac_priv *priv, u32 queue)
{
	struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[queue];
	u32 avail;

	if (tx_q->dirty_tx > tx_q->cur_tx)
		avail = tx_q->dirty_tx - tx_q->cur_tx - 1;
	else
		avail = DMA_TX_SIZE - tx_q->cur_tx + tx_q->dirty_tx - 1;

	return avail;
}

/**
 * tc956xmac_rx_dirty - Get RX queue dirty
 * @priv: driver private structure
 * @queue: RX queue index
 */
static inline u32 tc956xmac_rx_dirty(struct tc956xmac_priv *priv, u32 queue)
{
	struct tc956xmac_rx_queue *rx_q = &priv->rx_queue[queue];
	u32 dirty;

	if (rx_q->dirty_rx <= rx_q->cur_rx)
		dirty = rx_q->cur_rx - rx_q->dirty_rx;
	else
		dirty = DMA_RX_SIZE - rx_q->dirty_rx + rx_q->cur_rx;

	return dirty;
}

/**
 * tc956xmac_enable_eee_mode - check and enter in LPI mode
 * @priv: driver private structure
 * Description: this function is to verify and enter in LPI mode in case of
 * EEE.
 */
static void tc956xmac_enable_eee_mode(struct tc956xmac_priv *priv)
{
	tc956xmac_set_eee_mode(priv, priv->hw,
			priv->plat->en_tx_lpi_clockgating);
}

/**
 * tc956xmac_disable_eee_mode - disable and exit from LPI mode
 * @priv: driver private structure
 * Description: this function is to exit and disable EEE in case of
 * LPI state is true. This is called by the xmit.
 */
void tc956xmac_disable_eee_mode(struct tc956xmac_priv *priv)
{
	tc956xmac_reset_eee_mode(priv, priv->hw);
}

/**
 * tc956xmac_eee_init - init EEE
 * @priv: driver private structure
 * Description:
 *  if the GMAC supports the EEE (from the HW cap reg) and the phy device
 *  can also manage EEE, this function enable the LPI state and start related
 *  timer.
 */
bool tc956xmac_eee_init(struct tc956xmac_priv *priv)
{
#ifdef EEE_MAC_CONTROLLED_MODE
	int value;
#endif

	/* Using PCS we cannot dial with the phy registers at this stage
	 * so we do not support extra feature like EEE.
	 */
	if ((priv->hw->pcs == TC956XMAC_PCS_RGMII) ||
	    (priv->hw->pcs == TC956XMAC_PCS_TBI) ||
	    (priv->hw->pcs == TC956XMAC_PCS_RTBI))
		return false;

	/* Check if MAC core supports the EEE feature. */
	if (!priv->dma_cap.eee)
		return false;

	mutex_lock(&priv->lock);

	tc956xmac_enable_eee_mode(priv);
#ifdef EEE_MAC_CONTROLLED_MODE
	tc956xmac_set_eee_timer(priv, priv->hw, TC956XMAC_LIT_LS, TC956XMAC_TWT_LS);
	value = TC956XMAC_TIC_1US_CNTR;
	writel(value, priv->ioaddr + XGMAC_LPI_1US_Tic_Counter);
	value = readl(priv->ioaddr + XGMAC_LPI_Auto_Entry_Timer);
	/* Setting LPIET bit [19...3] */
	value &= ~(XGMAC_LPIET);
	/* LPI Entry timer is in the units of 8 micro second granularity considering last reserved 2:0 bits as zero 
	 * So mask the last 3 bits
	 */
	value |= (priv->tx_lpi_timer & XGMAC_LPIET);
	DBGPR_FUNC(priv->device, "%s Writing LPI timer value of [%d]\n", __func__, value);
	writel(value, priv->ioaddr + XGMAC_LPI_Auto_Entry_Timer);
#endif
	mutex_unlock(&priv->lock);
	netdev_dbg(priv->dev, "Energy-Efficient Ethernet initialized\n");
	return true;
}

/* tc956xmac_get_tx_hwtstamp - get HW TX timestamps
 * @priv: driver private structure
 * @p : descriptor pointer
 * @skb : the socket buffer
 * Description :
 * This function will read timestamp from the descriptor & pass it to stack.
 * and also perform some sanity checks.
 */
static void tc956xmac_get_tx_hwtstamp(struct tc956xmac_priv *priv,
				   struct dma_desc *p, struct sk_buff *skb)
{
	struct skb_shared_hwtstamps shhwtstamp;
	bool found = false;
	u64 ns = 0;
#if defined(TX_LOGGING_TRACE)
	static unsigned int ccnt1 = 0, ccnt2 = 0, ccnt3 = 0;
	static unsigned int ccnt4 = 0, ccnt5 = 0, ccnt6 = 0;
	u32 qno = skb_get_queue_mapping(skb);
#endif

#ifdef PKT_RATE_DBG
	static unsigned int count = 0;
	static u64 prev_ns = 0;
	u64 rate;
#endif

	if (!priv->hwts_tx_en)
		return;

#if !defined(TX_LOGGING_TRACE)
	/* exit if skb doesn't support hw tstamp */
	if (likely(!skb || !(skb_shinfo(skb)->tx_flags & SKBTX_IN_PROGRESS)))
		return;
#endif
	/* check tx tstamp status */
	if (tc956xmac_get_tx_timestamp_status(priv, p)) {
		tc956xmac_get_timestamp(priv, p, priv->adv_ts, &ns);
		found = true;
	} else if (!tc956xmac_get_mac_tx_timestamp(priv, priv->hw, &ns)) {
		found = true;
	}

	if (found) {
		memset(&shhwtstamp, 0, sizeof(struct skb_shared_hwtstamps));
		shhwtstamp.hwtstamp = ns_to_ktime(ns);

		netdev_dbg(priv->dev, "get valid TX hw timestamp %llu\n", ns);
		/* pass tstamp to stack */
		skb_tstamp_tx(skb, &shhwtstamp);
	}

#if defined(TX_LOGGING_TRACE)
	if (found) {

#ifdef PKT_RATE_DBG
		if (count % 8000 == 0) {
			rate = (8000*1000*1000000000ULL)/(unsigned int)(ns - prev_ns);
#ifdef FPE
			KPRINT_INFO("Tx FPE mmc_tx_frag_cnt:%d\n", priv->mmc.mmc_tx_fpe_fragment_cntr);
#endif
			prev_ns = ns;
		}
		count++;
#endif

#ifdef FPE
		priv->mmc.mmc_tx_fpe_fragments += readl(priv->mmcaddr + MMC_TX_FPE_FRAG);
#endif

		if (qno == AVB_CLASS_A_TX_CH) {/* For AVB */

			if (skb->data[20] == 0)
				ccnt1++;

#ifdef FPE
		trace_printk("[AVB]TS,%llu,%d,%03d,%02d,\n",
				ns, ccnt1, skb->data[20], qno);
#else
		trace_printk("[AVB]TS,%llu,%d,%03d,%02d, ,\n",
				ns, ccnt1, skb->data[20], qno);
#endif

		} else if (qno == AVB_CLASS_B_TX_CH) {

			if (skb->data[20] == 0)
				ccnt2++;

			trace_printk("[AVB_B]TS,%llu,%d,%03d,%02d, ,\n",
					ns, ccnt2, skb->data[20], qno);

		} else if (qno == TSN_CLASS_CDT_TX_CH) {/*  For CDT */

			if (skb->data[20] == 0)
				ccnt3++;

#ifdef FPE
			trace_printk("[CDT]TS,%llu,%d,%03d,%02d,\n",
					ns, ccnt3, skb->data[20], qno);
#else
			trace_printk("[CDT]TS,%llu,%d,%03d,%02d, ,\n",
					ns, ccnt3, skb->data[20], qno);
#endif
		} else if (qno == 4) { /*  For queue 3 */

			if (skb->data[20] == 0)
				/*trace_printk("[CDT]CYCLE = %d\n",ccnt3);*/
				ccnt4++;

			/* [CDT]TS,<timestamp>,<cycle iteration>,<sequence no>,<queue no>"	*/
#ifdef FPE
			trace_printk("[DUM4]TS,%llu,%d,%03d,%02d,\n",
					ns, ccnt4, skb->data[20], qno);
#else
			trace_printk("[DUM4]TS,%llu,%d,%03d,%02d, ,\n",
					ns, ccnt4, skb->data[20], qno);
#endif
		} else if (qno == 3) { /*  For queue 3 */

			if (skb->data[20] == 0)
				/*trace_printk("[CDT]CYCLE = %d\n",ccnt3);*/
				ccnt5++;

			/* [CDT]TS,<timestamp>,<cycle iteration>,<sequence no>,<queue no>"	*/
#ifdef FPE
				trace_printk("[DUM3]TS,%llu,%d,%03d,%02d,\n",
						ns, ccnt5, skb->data[20], qno);
#else
				trace_printk("[DUM3]TS,%llu,%d,%03d,%02d, ,\n",
						ns, ccnt5, skb->data[20], qno);
#endif

		} else if (qno == TC956X_GPTP_TX_CH) {
			u16 gPTP_ID = 0;
			u16 MsgType = 0;

			MsgType = skb->data[14] & 0x0F;
			gPTP_ID = skb->data[44];
			gPTP_ID = (gPTP_ID<<8) | skb->data[45];

			if (MsgType != 0x0b)
				trace_printk("[gPTP]TS,%019llu,%04d,0x%x,%02d\n",
						ns, gPTP_ID, MsgType, qno);

		} else if (qno == 1) { /*  For queue 1 dummy packet */

			if (skb->data[20] == 0)
				/*trace_printk("[CDT]CYCLE = %d\n",ccnt3);*/
				ccnt6++;

			/* [CDT]TS,<timestamp>,<cycle iteration>,<sequence no>,<queue no>" */
#ifdef FPE
				trace_printk("[DUM1]TS,%llu,%d,%03d,%02d,\n",
						ns, ccnt6, skb->data[20], qno);
#else
				trace_printk("[DUM1]TS,%llu,%d,%03d,%02d, ,\n",
						ns, ccnt6, skb->data[20], qno);
#endif

		} else if (qno == HOST_BEST_EFF_CH) {
#ifdef FPE
			trace_printk("[LE]TS,%llu,00,000,%02d,\n", ns, qno);
#else
			trace_printk("[LE]TS,%llu,00,000,%02d, ,\n", ns, qno);
#endif
		} else {
		}
	}
#endif

}

/* tc956xmac_get_rx_hwtstamp - get HW RX timestamps
 * @priv: driver private structure
 * @p : descriptor pointer
 * @np : next descriptor pointer
 * @skb : the socket buffer
 * Description :
 * This function will read received packet's timestamp from the descriptor
 * and pass it to stack. It also perform some sanity checks.
 */

#if defined(RX_LOGGING_TRACE)
static void tc956xmac_get_rx_hwtstamp(struct tc956xmac_priv *priv, struct dma_desc *p,
				   struct dma_desc *np, struct sk_buff *skb,
				   u32 qno)

#else
static void tc956xmac_get_rx_hwtstamp(struct tc956xmac_priv *priv, struct dma_desc *p,
				   struct dma_desc *np, struct sk_buff *skb)
#endif
{
	struct skb_shared_hwtstamps *shhwtstamp = NULL;
	struct dma_desc *desc = p;
	u64 ns = 0;
#if defined(RX_LOGGING_TRACE)
	static unsigned int ccnt1, ccnt2;
	unsigned int proto = 0;
#endif

#ifdef FPE
#if defined(RX_LOGGING_TRACE)
	priv->mmc.mmc_rx_pkt_assembly_ok += readl(priv->mmcaddr + MMC_RX_PKT_ASSEMBLY_OK);
	priv->mmc.mmc_rx_fpe_fragment += readl(priv->mmcaddr + MMC_RX_FPE_FRAG);
#endif
#endif

	if (!priv->hwts_rx_en)
		return;


	/* For GMAC4, the valid timestamp is from CTX next desc. */
	if (priv->plat->has_gmac4 || priv->plat->has_xgmac)
		desc = np;

	/* Check if timestamp is available */
	if (tc956xmac_get_rx_timestamp_status(priv, p, np, priv->adv_ts)) {
#ifdef FPE
#if defined(RX_LOGGING_TRACE)
		static unsigned int count = 0;

		if (count % 8000 == 0) {
			KPRINT_INFO("Rx FPE asmbly_ok_cnt:%d,frag_cnt:%d\n",
				priv->mmc.mmc_rx_pkt_assembly_ok,
				priv->mmc.mmc_rx_fpe_fragment);
		}
		count++;
#endif
#endif
		tc956xmac_get_timestamp(priv, desc, priv->adv_ts, &ns);
		netdev_dbg(priv->dev, "get valid RX hw timestamp %llu\n", ns);
		shhwtstamp = skb_hwtstamps(skb);
		memset(shhwtstamp, 0, sizeof(struct skb_shared_hwtstamps));
		shhwtstamp->hwtstamp = ns_to_ktime(ns);

#if defined(RX_LOGGING_TRACE)
		proto = htons(((skb->data[13]<<8) | skb->data[12]));
		if (proto == ETH_P_8021Q) {
			proto = htons(((skb->data[17]<<8) | skb->data[16]));
			if (proto == ETH_P_TSN) {
				if (skb->data[24] == 0 && skb->data[25] == 2) {
				/* Differentiated by stream IDs 2 for CDT */
					if ((unsigned char)skb->data[16] == 0)
						ccnt1++;

					trace_printk("[CDT]TS,%llu,%d,%03d,%02d \n",
							ns, ccnt1, skb->data[16], qno);
				} else if (skb->data[24] == 0 && skb->data[25] == 1) {
					/* Differentiated by stream IDs 1 for AVB */

					if ((unsigned char)skb->data[16] == 0)
						ccnt2++;

					    trace_printk("[AVB]TS,%llu,%d,%03d,\
							%02d \n", ns, ccnt2,
							skb->data[16], qno);
				}
			}
		} else if (proto == ETH_P_1588) {
			if ((skb->data[14]&0x0F) != 0xb) {
				u16 MsgType = skb->data[14] & 0x0F;
				u16 gPTP_ID = (skb->data[44] << 8) | skb->data[45];

				trace_printk("[gPTP]TS,%019llu,%04d,0x%x,%02d\n",
						ns, gPTP_ID, MsgType, qno);
			}
		} else {
		}
#endif
	} else {
		netdev_dbg(priv->dev, "cannot get RX hw timestamp\n");
	}
}

#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
/**
 *  tc956xmac_hwtstamp_set - control hardware timestamping.
 *  @dev: device pointer.
 *  @ifr: An IOCTL specific structure, that can contain a pointer to
 *  a proprietary structure used to pass information to the driver.
 *  Description:
 *  This function configures the MAC to enable/disable both outgoing(TX)
 *  and incoming(RX) packets time stamping based on user input.
 *  Return Value:
 *  0 on success and an appropriate -ve integer on failure.
 */
static int tc956xmac_hwtstamp_set(struct net_device *dev, struct ifreq *ifr)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
	struct hwtstamp_config config;
	u32 ptp_v2 = 0;
	u32 tstamp_all = 0;
	u32 ptp_over_ipv4_udp = 0;
	u32 ptp_over_ipv6_udp = 0;
	u32 ptp_over_ethernet = 0;
	u32 snap_type_sel = 0;
	u32 ts_master_en = 0;
	u32 ts_event_en = 0;
	u32 value = 0;
	u32 tcr_config = 0;
	bool xmac;

	xmac = priv->plat->has_gmac4 || priv->plat->has_xgmac;

	if (!(priv->dma_cap.time_stamp || priv->adv_ts)) {
		netdev_alert(priv->dev, "No support for HW time stamping\n");
		priv->hwts_tx_en = 0;
		priv->hwts_rx_en = 0;

		return -EOPNOTSUPP;
	}

	if (copy_from_user(&config, ifr->ifr_data,
			   sizeof(config)))
		return -EFAULT;

	netdev_dbg(priv->dev, "%s config flags:0x%x, tx_type:0x%x, rx_filter:0x%x\n",
		   __func__, config.flags, config.tx_type, config.rx_filter);

	/* reserved for future extensions */
	if (config.flags)
		return -EINVAL;

	if (config.tx_type != HWTSTAMP_TX_OFF &&
	    config.tx_type != HWTSTAMP_TX_ON)
		return -ERANGE;

	if (priv->adv_ts) {
		switch (config.rx_filter) {
		case HWTSTAMP_FILTER_NONE:
			/* time stamp no incoming packet at all */
			config.rx_filter = HWTSTAMP_FILTER_NONE;
			break;

		case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
			/* PTP v1, UDP, any kind of event packet */
			config.rx_filter = HWTSTAMP_FILTER_PTP_V1_L4_EVENT;
			/* 'xmac' hardware can support Sync, Pdelay_Req and
			 * Pdelay_resp by setting bit14 and bits17/16 to 01
			 * This leaves Delay_Req timestamps out.
			 * Enable all events *and* general purpose message
			 * timestamping
			 */
			snap_type_sel = PTP_TCR_SNAPTYPSEL_1;
			ptp_over_ipv4_udp = PTP_TCR_TSIPV4ENA;
			ptp_over_ipv6_udp = PTP_TCR_TSIPV6ENA;
			break;

		case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
			/* PTP v1, UDP, Sync packet */
			config.rx_filter = HWTSTAMP_FILTER_PTP_V1_L4_SYNC;
			/* take time stamp for SYNC messages only */
			ts_event_en = PTP_TCR_TSEVNTENA;

			ptp_over_ipv4_udp = PTP_TCR_TSIPV4ENA;
			ptp_over_ipv6_udp = PTP_TCR_TSIPV6ENA;
			break;

		case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
			/* PTP v1, UDP, Delay_req packet */
			config.rx_filter = HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ;
			/* take time stamp for Delay_Req messages only */
			ts_master_en = PTP_TCR_TSMSTRENA;
			ts_event_en = PTP_TCR_TSEVNTENA;

			ptp_over_ipv4_udp = PTP_TCR_TSIPV4ENA;
			ptp_over_ipv6_udp = PTP_TCR_TSIPV6ENA;
			break;

		case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
			/* PTP v2, UDP, any kind of event packet */
			config.rx_filter = HWTSTAMP_FILTER_PTP_V2_L4_EVENT;
			ptp_v2 = PTP_TCR_TSVER2ENA;
			/* take time stamp for all event messages */
			snap_type_sel = PTP_TCR_SNAPTYPSEL_1;

			ptp_over_ipv4_udp = PTP_TCR_TSIPV4ENA;
			ptp_over_ipv6_udp = PTP_TCR_TSIPV6ENA;
			break;

		case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
			/* PTP v2, UDP, Sync packet */
			config.rx_filter = HWTSTAMP_FILTER_PTP_V2_L4_SYNC;
			ptp_v2 = PTP_TCR_TSVER2ENA;
			/* take time stamp for SYNC messages only */
			ts_event_en = PTP_TCR_TSEVNTENA;

			ptp_over_ipv4_udp = PTP_TCR_TSIPV4ENA;
			ptp_over_ipv6_udp = PTP_TCR_TSIPV6ENA;
			break;

		case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
			/* PTP v2, UDP, Delay_req packet */
			config.rx_filter = HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ;
			ptp_v2 = PTP_TCR_TSVER2ENA;
			/* take time stamp for Delay_Req messages only */
			ts_master_en = PTP_TCR_TSMSTRENA;
			ts_event_en = PTP_TCR_TSEVNTENA;

			ptp_over_ipv4_udp = PTP_TCR_TSIPV4ENA;
			ptp_over_ipv6_udp = PTP_TCR_TSIPV6ENA;
			break;

		case HWTSTAMP_FILTER_PTP_V2_EVENT:
			/* PTP v2/802.AS1 any layer, any kind of event packet */
			config.rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
			ptp_v2 = PTP_TCR_TSVER2ENA;
			snap_type_sel = PTP_TCR_SNAPTYPSEL_1;
			ts_event_en = PTP_TCR_TSEVNTENA;
			ptp_over_ipv4_udp = PTP_TCR_TSIPV4ENA;
			ptp_over_ipv6_udp = PTP_TCR_TSIPV6ENA;
			ptp_over_ethernet = PTP_TCR_TSIPENA;
			break;

		case HWTSTAMP_FILTER_PTP_V2_SYNC:
			/* PTP v2/802.AS1, any layer, Sync packet */
			config.rx_filter = HWTSTAMP_FILTER_PTP_V2_SYNC;
			ptp_v2 = PTP_TCR_TSVER2ENA;
			/* take time stamp for SYNC messages only */
			ts_event_en = PTP_TCR_TSEVNTENA;

			ptp_over_ipv4_udp = PTP_TCR_TSIPV4ENA;
			ptp_over_ipv6_udp = PTP_TCR_TSIPV6ENA;
			ptp_over_ethernet = PTP_TCR_TSIPENA;
			break;

		case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
			/* PTP v2/802.AS1, any layer, Delay_req packet */
			config.rx_filter = HWTSTAMP_FILTER_PTP_V2_DELAY_REQ;
			ptp_v2 = PTP_TCR_TSVER2ENA;
			/* take time stamp for Delay_Req messages only */
			ts_master_en = PTP_TCR_TSMSTRENA;
			ts_event_en = PTP_TCR_TSEVNTENA;

			ptp_over_ipv4_udp = PTP_TCR_TSIPV4ENA;
			ptp_over_ipv6_udp = PTP_TCR_TSIPV6ENA;
			ptp_over_ethernet = PTP_TCR_TSIPENA;
			break;

		case HWTSTAMP_FILTER_NTP_ALL:
		case HWTSTAMP_FILTER_ALL:
			/* time stamp any incoming packet */
			config.rx_filter = HWTSTAMP_FILTER_ALL;
			tstamp_all = PTP_TCR_TSENALL;
			break;

		default:
			return -ERANGE;
		}
	} else {
		switch (config.rx_filter) {
		case HWTSTAMP_FILTER_NONE:
			config.rx_filter = HWTSTAMP_FILTER_NONE;
			break;
		default:
			/* PTP v1, UDP, any kind of event packet */
			config.rx_filter = HWTSTAMP_FILTER_PTP_V1_L4_EVENT;
			break;
		}
	}
	priv->hwts_rx_en = ((config.rx_filter == HWTSTAMP_FILTER_NONE) ? 0 : 1);
	priv->hwts_tx_en = config.tx_type == HWTSTAMP_TX_ON;

	if (!priv->hwts_tx_en && !priv->hwts_rx_en)
		tc956xmac_config_hw_tstamping(priv, priv->ptpaddr, 0);
	else {
		tcr_config = (PTP_TCR_TSENA | PTP_TCR_TSCFUPDT | PTP_TCR_TSCTRLSSR |
			tstamp_all | ptp_v2 | ptp_over_ethernet |
			ptp_over_ipv6_udp | ptp_over_ipv4_udp | ts_event_en |
			ts_master_en | snap_type_sel | PTP_TCR_ASMEN);

		value = readl(priv->ptpaddr + PTP_TCR);
		/* Note : Values will never be set. The IOCTL will
		 *not set any bits
		 */
		if (!(value & 0x00000001)) {
			tc956x_ptp_configuration(priv, tcr_config);
			DBGPR_FUNC(priv->device, "--> %s\n", __func__);
		}

	}

	memcpy(&priv->tstamp_config, &config, sizeof(config));

	return copy_to_user(ifr->ifr_data, &config,
			    sizeof(config)) ? -EFAULT : 0;
}

/*
 *  tc956xmac_hwtstamp_get - read hardware timestamping.
 *  @dev: device pointer.
 *  @ifr: An IOCTL specific structure, that can contain a pointer to
 *  a proprietary structure used to pass information to the driver.
 *  Description:
 *  This function obtain the current hardware timestamping settings
 *  as requested.
 */
static int tc956xmac_hwtstamp_get(struct net_device *dev, struct ifreq *ifr)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
	struct hwtstamp_config *config = &priv->tstamp_config;

	if (!(priv->dma_cap.time_stamp || priv->dma_cap.atime_stamp))
		return -EOPNOTSUPP;

	return copy_to_user(ifr->ifr_data, config,
			    sizeof(*config)) ? -EFAULT : 0;
}
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */

/**
 * tc956xmac_init_ptp - init PTP
 * @priv: driver private structure
 * Description: this is to verify if the HW supports the PTPv1 or PTPv2.
 * This is done by looking at the HW cap. register.
 * This function also registers the ptp driver.
 */
static int tc956xmac_init_ptp(struct tc956xmac_priv *priv)
{
	bool xmac = priv->plat->has_gmac4 || priv->plat->has_xgmac;

	if (!(priv->dma_cap.time_stamp || priv->dma_cap.atime_stamp))
		return -EOPNOTSUPP;

	priv->adv_ts = 0;
	/* Check if adv_ts can be enabled for dwmac 4.x / xgmac core */
	if (xmac && priv->dma_cap.atime_stamp)
		priv->adv_ts = 1;
	/* Dwmac 3.x core with extend_desc can support adv_ts */
	else if (priv->extend_desc && priv->dma_cap.atime_stamp)
		priv->adv_ts = 1;

	if (priv->dma_cap.time_stamp)
		netdev_info(priv->dev, "IEEE 1588-2002 Timestamp supported\n");

	if (priv->adv_ts)
		netdev_info(priv->dev,
			    "IEEE 1588-2008 Advanced Timestamp supported\n");

	priv->hwts_tx_en = 0;
	priv->hwts_rx_en = 0;

	tc956xmac_ptp_register(priv);

	return 0;
}

static void tc956xmac_release_ptp(struct tc956xmac_priv *priv)
{
	if (priv->plat->clk_ptp_ref)
		clk_disable_unprepare(priv->plat->clk_ptp_ref);
	tc956xmac_ptp_unregister(priv);
}

/**
 *  tc956xmac_mac_flow_ctrl - Configure flow control in all queues
 *  @priv: driver private structure
 *  Description: It is used for configuring the flow control in all queues
 */
static void tc956xmac_mac_flow_ctrl(struct tc956xmac_priv *priv, u32 duplex)
{
	u32 tx_cnt = priv->plat->tx_queues_to_use;

	tc956xmac_flow_ctrl(priv, priv->hw, duplex, priv->flow_ctrl,
			priv->pause, tx_cnt);
}

static void tc956xmac_validate(struct phylink_config *config,
			    unsigned long *supported,
			    struct phylink_link_state *state)
{
	struct tc956xmac_priv *priv = netdev_priv(to_net_dev(config->dev));
	__ETHTOOL_DECLARE_LINK_MODE_MASK(mac_supported) = { 0, };
	__ETHTOOL_DECLARE_LINK_MODE_MASK(mask) = { 0, };
	int tx_cnt = priv->plat->tx_queues_to_use;
	int max_speed = priv->plat->max_speed;

	phylink_set(mac_supported, 10baseT_Half);
	phylink_set(mac_supported, 10baseT_Full);
	phylink_set(mac_supported, 100baseT_Half);
	phylink_set(mac_supported, 100baseT_Full);
	phylink_set(mac_supported, 1000baseT_Half);
	phylink_set(mac_supported, 1000baseT_Full);
	phylink_set(mac_supported, 1000baseKX_Full);

	phylink_set(mac_supported, Autoneg);
	phylink_set(mac_supported, Pause);
	phylink_set(mac_supported, Asym_Pause);
	phylink_set_port_modes(mac_supported);

	/*USXGMII interface does not support speed of 1000/100/10*/
	if (priv->plat->interface == PHY_INTERFACE_MODE_USXGMII) {
		phylink_set(mask, 10baseT_Half);
		phylink_set(mask, 10baseT_Full);
		phylink_set(mask, 100baseT_Half);
		phylink_set(mask, 100baseT_Full);
		phylink_set(mask, 1000baseT_Half);
		phylink_set(mask, 1000baseT_Full);
		phylink_set(mask, 1000baseKX_Full);
	}


	/* Cut down 1G if asked to */
	if ((max_speed > 0) && (max_speed < 1000)) {
		phylink_set(mask, 1000baseT_Full);
		phylink_set(mask, 1000baseX_Full);
	} else if (priv->plat->has_xgmac) {
		if (!max_speed || (max_speed >= 2500)) {
			phylink_set(mac_supported, 2500baseT_Full);
			phylink_set(mac_supported, 2500baseX_Full);
		}
		if (!max_speed || (max_speed >= 5000))
			phylink_set(mac_supported, 5000baseT_Full);
		if (!max_speed || (max_speed >= 10000)) {
			phylink_set(mac_supported, 10000baseSR_Full);
			phylink_set(mac_supported, 10000baseLR_Full);
			phylink_set(mac_supported, 10000baseER_Full);
			phylink_set(mac_supported, 10000baseLRM_Full);
			phylink_set(mac_supported, 10000baseT_Full);
			phylink_set(mac_supported, 10000baseKX4_Full);
			phylink_set(mac_supported, 10000baseKR_Full);
		}
	}

	/* Half-Duplex can only work with single queue */
	if (tx_cnt > 1) {
#ifdef TC956X
		KPRINT_INFO("Half duplex not supported in Port0");
#else
		phylink_set(mask, 10baseT_Half);
		phylink_set(mask, 100baseT_Half);
#endif

#ifdef TC956X
		phylink_set(mask, 1000baseT_Half);
#endif
	}

	bitmap_and(supported, supported, mac_supported,
			__ETHTOOL_LINK_MODE_MASK_NBITS);
	bitmap_andnot(supported, supported, mask,
			__ETHTOOL_LINK_MODE_MASK_NBITS);
	bitmap_and(state->advertising, state->advertising, mac_supported,
			__ETHTOOL_LINK_MODE_MASK_NBITS);
	bitmap_andnot(state->advertising, state->advertising, mask,
			__ETHTOOL_LINK_MODE_MASK_NBITS);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 5, 0))
static void tc956xmac_mac_pcs_get_state(struct phylink_config *config,
					struct phylink_link_state *state)
{
#ifndef TC956X
	state->link = 0;
#else
	struct tc956xmac_priv *priv = netdev_priv(to_net_dev(config->dev));
	u32 reg_value;

	reg_value = tc956x_xpcs_read(priv->xpcsaddr, XGMAC_VR_MII_AN_INTR_STS);
	if (reg_value & XGMAC_C37_AN_COMPL) {/*check if AN 37 is complete CL37_ANCMPLT_INTR*/
		KPRINT_INFO("AN clause 37 completed");
		if ((priv->plat->interface == PHY_INTERFACE_MODE_USXGMII) ||
		   (priv->plat->interface == PHY_INTERFACE_MODE_10GKR)) {
			if (reg_value & XGMAC_USXG_AN_STS_LINK_MASK) {/*check link status*/
				state->link = 1;
				KPRINT_INFO("XPCS USXGMII link up");

				if (((reg_value & XGMAC_USXG_AN_STS_SPEED_MASK) >> 9) == 0x3)/*If USXG_AN_STS is 10G*/
					state->speed = SPEED_10000;
				else if (((reg_value & XGMAC_USXG_AN_STS_SPEED_MASK) >> 9) == 0x5)/*If USXG_AN_STS is 5G*/
					state->speed = SPEED_5000;
				else if (((reg_value & XGMAC_USXG_AN_STS_SPEED_MASK) >> 9) == 0x4)/*If USXG_AN_STS is 5G*/
					state->speed = SPEED_2500;

				if (reg_value & XGMAC_USXG_AN_STS_DUPLEX_MASK)/*If USXG_AN_STS is full duplex*/
					state->duplex = DUPLEX_FULL;
				else
					state->duplex = DUPLEX_HALF;
			} else {
				KPRINT_INFO("XPCS USXGMII link down");
				state->link = 0;
			}

		} else if (priv->plat->interface == PHY_INTERFACE_MODE_SGMII) {
			if (reg_value & XGMAC_SGM_STS_LINK_MASK) {/*SGMII autonegotiated link */
				state->link = 1;
				KPRINT_INFO("XPCS SGMII link up");

				if (reg_value & XGMAC_SGM_STS_DUPLEX)/*SGMII autonegotiated duplex */
					state->duplex = DUPLEX_FULL;
				else
					state->duplex = DUPLEX_HALF;

				if (((reg_value & XGMAC_SGM_STS_SPEED_MASK) >> 2) == 0x2) {/*SGMII autonegotiated speed */
					if (priv->is_sgmii_2p5g == true)
						state->speed = SPEED_2500; /*There is no seperate bit check for 2.5Gbps, so set here */
					else
						state->speed = SPEED_1000;
				} else if (((reg_value & XGMAC_SGM_STS_SPEED_MASK) >> 2) == 0x1)
					state->speed = SPEED_100;
			} else {
				state->link = 0;
				KPRINT_INFO("XPCS SGMII link down");
			}
		}
	}
#endif
}
#else	/* Required when using with Kernel v5.4 */
static int tc956xmac_mac_link_state(struct phylink_config *config,
				 struct phylink_link_state *state)
{
#ifndef TC956X
	return -EOPNOTSUPP;
#else
	struct tc956xmac_priv *priv = netdev_priv(to_net_dev(config->dev));
	u32 reg_value;

	reg_value = tc956x_xpcs_read(priv->xpcsaddr, XGMAC_VR_MII_AN_INTR_STS);
	if (reg_value & XGMAC_C37_AN_COMPL) {/*check if AN 37 is complete CL37_ANCMPLT_INTR*/
		KPRINT_INFO("AN clause 37 completed");
		if ((priv->plat->interface == PHY_INTERFACE_MODE_USXGMII) ||
		   (priv->plat->interface == PHY_INTERFACE_MODE_10GKR)) {
			if (reg_value & XGMAC_USXG_AN_STS_LINK_MASK) {/*check link status*/
				state->link = 1;
				KPRINT_INFO("XPCS USXGMII link up");

				/*If USXG_AN_STS is 10G*/
				if (((reg_value & XGMAC_USXG_AN_STS_SPEED_MASK) >> 9) == 0x3)
					state->speed = SPEED_10000;
				/*If USXG_AN_STS is 5G*/
				else if (((reg_value & XGMAC_USXG_AN_STS_SPEED_MASK) >> 9) == 0x5)
					state->speed = SPEED_5000;
				/*If USXG_AN_STS is 5G*/
				else if (((reg_value & XGMAC_USXG_AN_STS_SPEED_MASK) >> 9) == 0x4)
					state->speed = SPEED_2500;

				/*If USXG_AN_STS is full duplex*/
				if (reg_value & XGMAC_USXG_AN_STS_DUPLEX_MASK)
					state->duplex = DUPLEX_FULL;
				else
					state->duplex = DUPLEX_HALF;
			} else {
				KPRINT_INFO("XPCS USXGMII link down");
				state->link = 0;
			}

		} else if (priv->plat->interface == PHY_INTERFACE_MODE_SGMII) {
			if (reg_value & XGMAC_SGM_STS_LINK_MASK) {/*SGMII autonegotiated link */
				state->link = 1;
				KPRINT_INFO("XPCS SGMII link up");
				/*SGMII autonegotiated duplex */
				if (reg_value & XGMAC_SGM_STS_DUPLEX)
					state->duplex = DUPLEX_FULL;
				else
					state->duplex = DUPLEX_HALF;
				/*SGMII autonegotiated speed */
				if (((reg_value & XGMAC_SGM_STS_SPEED_MASK) >> 2) == 0x2) {
					if (priv->is_sgmii_2p5g == true)
						state->speed = SPEED_2500; /*There is no seperate bit check for 2.5Gbps, so set here */
					else
						state->speed = SPEED_1000;
				} else if (((reg_value & XGMAC_SGM_STS_SPEED_MASK) >> 2) == 0x1)
					state->speed = SPEED_100;
			} else {
				state->link = 0;
				KPRINT_INFO("XPCS SGMII link down");
			}
		}
	}

	return 0;
#endif
}
#endif

/**
 *  tc956xmac_speed_change_init_mac - Initialize MAC during speed change.
 *  @priv: driver private structure
 *  @state : phy state structure
 *  Description: It is used for initializing MAC during speed change of
 *  USXGMII and SGMII.
 */
void tc956xmac_speed_change_init_mac(struct tc956xmac_priv *priv,
					const struct phylink_link_state *state)
{
	/* use signal from MSPHY */
	uint8_t SgmSigPol = 0;
	int ret = 0;
	bool enable_an = true;

	if (priv->port_num == RM_PF0_ID) {
		/* Enable all clocks to eMAC Port0 */
		ret = readl(priv->tc956x_SFR_pci_base_addr + NCLKCTRL0_OFFSET);
		if ((state->interface == PHY_INTERFACE_MODE_SGMII) &&
		   (state->speed == SPEED_2500)) {
			ret &= ~NCLKCTRL0_MAC0125CLKEN;
			ret &= ~NCLKCTRL0_MAC0312CLKEN;
		}
		writel(ret, priv->tc956x_SFR_pci_base_addr + NCLKCTRL0_OFFSET);

		/* Interface configuration for port0*/
		ret = readl(priv->tc956x_SFR_pci_base_addr + NEMAC0CTL_OFFSET);
		ret &= ~(NEMACCTL_SP_SEL_MASK | NEMACCTL_PHY_INF_SEL_MASK);
		if (state->interface == PHY_INTERFACE_MODE_SGMII) {
			if (state->speed == SPEED_2500)
				ret |= NEMACCTL_SP_SEL_SGMII_2500M;
			else
				ret |= NEMACCTL_SP_SEL_SGMII_1000M;
		} else {
			/*else if ((PORT0_INTERFACE == ENABLE_USXGMII_INTERFACE)*/
			if (state->speed == SPEED_10000)
				ret |= NEMACCTL_SP_SEL_USXGMII_10G_10G;
			else if (state->speed == SPEED_5000)
				ret |= NEMACCTL_SP_SEL_USXGMII_5G_10G;
			else if (state->speed == SPEED_2500)
				ret |= NEMACCTL_SP_SEL_USXGMII_2_5G_10G;
		}
		ret &= ~(0x00000040); /* Mask Polarity */
		if (SgmSigPol == 1)
			ret |= 0x00000040; /* Set Active low */
		ret |= (NEMACCTL_PHY_INF_SEL | NEMACCTL_LPIHWCLKEN);
		writel(ret, priv->tc956x_SFR_pci_base_addr + NEMAC0CTL_OFFSET);
	}
	if (priv->port_num == RM_PF1_ID) {
		/* Enable all clocks to eMAC Port1 */
		ret = readl(priv->tc956x_SFR_pci_base_addr + NCLKCTRL1_OFFSET);
		if ((state->interface == PHY_INTERFACE_MODE_SGMII) &&
		   (state->speed == SPEED_2500)) {
			ret &= ~NCLKCTRL1_MAC1125CLKEN1;
			ret &= ~NCLKCTRL1_MAC1312CLKEN1;
		} else {
			ret &= ~NCLKCTRL1_MAC1312CLKEN1;
			ret |= NCLKCTRL1_MAC1125CLKEN1;
		}
		writel(ret, priv->tc956x_SFR_pci_base_addr + NCLKCTRL1_OFFSET);

		/* Interface configuration for port1*/
		ret = readl(priv->tc956x_SFR_pci_base_addr + NEMAC1CTL_OFFSET);
		ret &= ~(NEMACCTL_SP_SEL_MASK | NEMACCTL_PHY_INF_SEL_MASK);
		if (state->interface == PHY_INTERFACE_MODE_SGMII) {
			if (state->speed == SPEED_2500)
				ret |= NEMACCTL_SP_SEL_SGMII_2500M;
			else
				ret |= NEMACCTL_SP_SEL_SGMII_1000M;
		}

		ret &= ~(0x00000040); /* Mask Polarity */
		if (SgmSigPol == 1)
			ret |= 0x00000040; /* Set Active low */
		ret |= (NEMACCTL_PHY_INF_SEL | NEMACCTL_LPIHWCLKEN);
		writel(ret, priv->tc956x_SFR_pci_base_addr + NEMAC1CTL_OFFSET);
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
			KPRINT_ERR("PMA switching to internal clock Failed\n");

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
		if ((state->interface == PHY_INTERFACE_MODE_SGMII)
		&& (state->speed == SPEED_2500)) {
			/* XPCS doesn't support AN for 2.5G SGMII.
			 * Disable AN only if SGMII 2.5G is Enabled.
			 */
			priv->is_sgmii_2p5g = true;
			enable_an = false;
		} else {
			priv->is_sgmii_2p5g = false;
			enable_an = true;
		}

		ret = tc956x_xpcs_init(priv, priv->xpcsaddr);
		if (ret < 0)
			KPRINT_INFO("XPCS initialization error\n");
		tc956x_xpcs_ctrl_ane(priv, enable_an);
	}
}

static void tc956xmac_mac_config(struct phylink_config *config, unsigned int mode,
				const struct phylink_link_state *state)
{
	struct tc956xmac_priv *priv = netdev_priv(to_net_dev(config->dev));
	u32 ctrl, emac_ctrl;
	u32 val;
	bool config_done = false;

#ifdef TC956X
	u32 reg_value;

	ctrl = readl(priv->ioaddr + MAC_CTRL_REG);
	ctrl &= ~priv->hw->link.speed_mask;

	emac_ctrl = readl(priv->ioaddr + NEMACCTL_OFFSET);
	emac_ctrl &= ~NEMACCTL_SP_SEL_MASK;

	if (priv->hw->xpcs) {
		reg_value = tc956x_xpcs_read(priv->xpcsaddr, XGMAC_VR_MII_AN_INTR_STS);
		if (reg_value & XGMAC_C37_AN_COMPL) {/*check if AN 37 is complete CL37_ANCMPLT_INTR*/
			KPRINT_INFO("AN clause 37 completed");
			reg_value &= ~(XGMAC_C37_AN_COMPL);
			tc956x_xpcs_write(priv->xpcsaddr, XGMAC_VR_MII_AN_INTR_STS, reg_value);
			KPRINT_INFO("AN clause 37 complete bit cleared");
		}

		if (state->interface == PHY_INTERFACE_MODE_USXGMII) {
			/* Invoke this only during speed change */
			if ((state->speed != SPEED_UNKNOWN) && (state->speed != 0)) {
				if (state->speed != priv->speed) {
					tc956xmac_speed_change_init_mac(priv, state);
				}
			} else {
				return;
			}

			/* Program autonegotiated speed to SR_MII_CTRL */
			val = tc956x_xpcs_read(priv->xpcsaddr, XGMAC_SR_MII_CTRL);
			val &= ~XGMAC_SR_MII_CTRL_SPEED; /* Mask speed ss13, ss6, ss5 */

			switch (state->speed) {
			case SPEED_10000:
				ctrl |= priv->hw->link.xgmii.speed10000;
				emac_ctrl |= NEMACCTL_SP_SEL_USXGMII_10G_10G;
				val |= XGMAC_SR_MII_CTRL_SPEED_10G;
				break;
			case SPEED_5000:
				ctrl |= priv->hw->link.xgmii.speed5000;
				emac_ctrl |= NEMACCTL_SP_SEL_USXGMII_5G_10G;
				val |= XGMAC_SR_MII_CTRL_SPEED_5G;
				break;
			case SPEED_2500:
				ctrl |= priv->hw->link.xgmii.speed2500;
				emac_ctrl |= NEMACCTL_SP_SEL_USXGMII_2_5G_10G;
				val |= XGMAC_SR_MII_CTRL_SPEED_2_5G;
				break;
			default:
				return;
			}

			tc956x_xpcs_write(priv->xpcsaddr, XGMAC_SR_MII_CTRL, val);

			/* USRA_RST set to 1 */
			val = tc956x_xpcs_read(priv->xpcsaddr, XGMAC_VR_XS_PCS_DIG_CTRL1);
			val |= XGMAC_USRA_RST;
			tc956x_xpcs_write(priv->xpcsaddr, XGMAC_VR_XS_PCS_DIG_CTRL1, val);
			config_done = true;
		}
		if (state->interface == PHY_INTERFACE_MODE_SGMII) { /* Autonegotiation not supported for SGMII */
			reg_value = tc956x_xpcs_read(priv->xpcsaddr, XGMAC_VR_MII_AN_INTR_STS);
			/* Clear autonegotiation only if completed. As for XPCS, 2.5G autonegotiation is not supported */
			/* Switching from SGMII 2.5G to any speed doesn't cause AN completion */
			if (reg_value & XGMAC_C37_AN_COMPL) {/*check if AN 37 is complete CL37_ANCMPLT_INTR*/
				KPRINT_INFO("AN clause 37 completed");
				reg_value &= ~(XGMAC_C37_AN_COMPL);
				tc956x_xpcs_write(priv->xpcsaddr, XGMAC_VR_MII_AN_INTR_STS, reg_value);
				KPRINT_INFO("AN clause 37 complete bit cleared");
			}
			/* Invoke this only during speed change */
			if ((state->speed != SPEED_UNKNOWN) && (state->speed != 0)) {
				if (state->speed != priv->speed)
					tc956xmac_speed_change_init_mac(priv, state);
			} else {
				return;
			}
			val = tc956x_xpcs_read(priv->xpcsaddr, XGMAC_SR_MII_CTRL);
			val &= ~XGMAC_SR_MII_CTRL_SPEED; /* Mask speed ss13, ss6, ss5 */
			switch (state->speed) {
			case SPEED_2500:
				ctrl |= priv->hw->link.speed2500;
				/* Program autonegotiated speed to SR_MII_CTRL */
				val |= XPCS_SS_SGMII_1G; /*1000 Mbps setting only available, so set the same*/
				emac_ctrl |= NEMACCTL_SP_SEL_SGMII_2500M;
				break;
			case SPEED_1000:
				ctrl |= priv->hw->link.speed1000;
				val |= XPCS_SS_SGMII_1G; /*1000 Mbps setting only available, so set the same*/
				emac_ctrl |= NEMACCTL_SP_SEL_SGMII_1000M;
				break;
			case SPEED_100:
				ctrl |= priv->hw->link.speed100;
				val |= XPCS_SS_SGMII_100M; /*100 Mbps setting */
				emac_ctrl |= NEMACCTL_SP_SEL_SGMII_100M;
				break;
			case SPEED_10:
				ctrl |= priv->hw->link.speed10;
				val |= XPCS_SS_SGMII_10M; /*10 Mbps setting */
				emac_ctrl |= NEMACCTL_SP_SEL_SGMII_10M;
				break;
			default:
				return;
			}
			tc956x_xpcs_write(priv->xpcsaddr, XGMAC_SR_MII_CTRL, val);
			config_done = true;
		}
	} else if (state->interface == PHY_INTERFACE_MODE_RGMII) {
		switch (state->speed) {
		case SPEED_1000:
			ctrl |= priv->hw->link.speed1000;
			emac_ctrl |= NEMACCTL_SP_SEL_RGMII_1000M;
			break;
		case SPEED_100:
			ctrl |= priv->hw->link.speed100;
			emac_ctrl |= NEMACCTL_SP_SEL_RGMII_100M;
			break;
		case SPEED_10:
			ctrl |= priv->hw->link.speed10;
			emac_ctrl |= NEMACCTL_SP_SEL_RGMII_10M;
			break;
		default:
			return;
		}
		config_done = true;
	} else {
		switch (state->speed) {
		case SPEED_2500:
			ctrl |= priv->hw->link.speed2500;
			break;
		case SPEED_1000:
			ctrl |= priv->hw->link.speed1000;
			break;
		case SPEED_100:
			ctrl |= priv->hw->link.speed100;
			break;
		case SPEED_10:
			ctrl |= priv->hw->link.speed10;
			break;
		default:
			return;
		}
		config_done = true;
	}

	priv->speed = state->speed;

	if (priv->plat->fix_mac_speed)
		priv->plat->fix_mac_speed(priv->plat->bsp_priv, state->speed);

	if (!state->duplex)
		ctrl &= ~priv->hw->link.duplex;
	else
		ctrl |= priv->hw->link.duplex;

	/* Flow Control operation */
	if (state->pause)
		tc956xmac_mac_flow_ctrl(priv, state->duplex);

	if (config_done) {
		writel(ctrl, priv->ioaddr + MAC_CTRL_REG);
		writel(emac_ctrl, priv->ioaddr + NEMACCTL_OFFSET);
	}
#endif
}

static void tc956xmac_mac_an_restart(struct phylink_config *config)
{
#ifdef TC956X
	struct tc956xmac_priv *priv = netdev_priv(to_net_dev(config->dev));
	bool enable_en = true;

	if (priv->hw->xpcs) {
		/*Enable XPCS Autoneg*/
		if (priv->plat->interface == PHY_INTERFACE_MODE_10GKR) {
			enable_en = false;
			KPRINT_INFO("%s :Port %d AN Enable:%d", __func__, priv->port_num, enable_en);
		} else if (priv->plat->interface == PHY_INTERFACE_MODE_SGMII) {
			if (priv->is_sgmii_2p5g == true) {
				enable_en = false;
				KPRINT_INFO("%s : Port %d AN Enable:%d", __func__, priv->port_num, enable_en);
			} else {
				enable_en = true;
				KPRINT_INFO("%s : Port %d AN Enable:%d", __func__, priv->port_num, enable_en);
			}
		} else {
			enable_en = true;
			KPRINT_INFO("%s : Port %d AN Enable:%d", __func__, priv->port_num, enable_en);
		}
		tc956x_xpcs_ctrl_ane(priv, enable_en);
	}
#else
	/*Not supported*/
#endif
}

static void tc956xmac_mac_link_down(struct phylink_config *config,
				 unsigned int mode, phy_interface_t interface)
{
	struct tc956xmac_priv *priv = netdev_priv(to_net_dev(config->dev));

	tc956xmac_mac_set_rx(priv, priv->ioaddr, false);
#ifdef EEE
	priv->eee_active = false;
	DBGPR_FUNC(priv->device, "%s Disable EEE\n", __func__);
	tc956xmac_disable_eee_mode(priv);
	tc956xmac_set_eee_pls(priv, priv->hw, false);
#endif
#ifdef TC956X_PM_DEBUG
	pm_generic_suspend(priv->device);
#endif
}

#ifdef TC956X_5_G_2_5_G_EEE_SUPPORT
static inline bool tc956x_phy_check_valid(int speed, int duplex,
				   unsigned long *features)
{
	return !!phy_lookup_setting(speed, duplex, features, true);
}

static void tc956x_mmd_eee_adv_to_linkmode_5G_2_5G(unsigned long *advertising, u16 eee_adv)
{
	linkmode_zero(advertising);

	if (eee_adv & MDIO_EEE_5GT)
		linkmode_set_bit(ETHTOOL_LINK_MODE_5000baseT_Full_BIT,
				 advertising);
	if (eee_adv & MDIO_EEE_2_5GT)
		linkmode_set_bit(ETHTOOL_LINK_MODE_2500baseT_Full_BIT,
				 advertising);
}

int tc956x_phy_init_eee(struct phy_device *phydev, bool clk_stop_enable)
{
	if (!phydev->drv)
		return -EIO;

	KPRINT_INFO("%s EEE phy init for 5G/2.5G\n", __func__);

	/* According to 802.3az,the EEE is supported only in full duplex-mode.
	 */
	if (phydev->duplex == DUPLEX_FULL) {
		__ETHTOOL_DECLARE_LINK_MODE_MASK(common);
		__ETHTOOL_DECLARE_LINK_MODE_MASK(lp);
		__ETHTOOL_DECLARE_LINK_MODE_MASK(adv);
		int eee_lp, eee_cap, eee_adv;
		int status;
		u32 cap;

		/* Read phy status to properly get the right settings */
		status = phy_read_status(phydev);
		if (status) {
			KPRINT_ERR("Error 0: %d\n", status);
			return status;
		}

		/* First check if the EEE ability is supported */
		eee_cap = phy_read_mmd(phydev, MDIO_MMD_PCS, MDIO_PCS_EEE_ABLE2);
		if (eee_cap <= 0) {
			KPRINT_ERR("Error 2\n");
			goto eee_exit_err;
		}

		cap = mmd_eee_cap_to_ethtool_sup_t(eee_cap);
		if (!cap) {
			KPRINT_ERR("Error 3\n");
			goto eee_exit_err;
		}

		/* Check which link settings negotiated and verify it in
		 * the EEE advertising registers.
		 */
		eee_lp = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_EEE_LPABLE2);
		if (eee_lp <= 0) {
			KPRINT_ERR("Error 4\n");
			goto eee_exit_err;
		}

		eee_adv = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_EEE_ADV2);
		if (eee_adv <= 0) {
			KPRINT_ERR("Error 5\n");
			goto eee_exit_err;
		}

		tc956x_mmd_eee_adv_to_linkmode_5G_2_5G(adv, eee_adv);
		tc956x_mmd_eee_adv_to_linkmode_5G_2_5G(lp, eee_lp);
		linkmode_and(common, adv, lp);

		if (!tc956x_phy_check_valid(phydev->speed, phydev->duplex, common)) {
			KPRINT_ERR("Error 6\n");
			goto eee_exit_err;
		}

		if (clk_stop_enable)
			phy_set_bits_mmd(phydev, MDIO_MMD_PCS, MDIO_CTRL1,
					 MDIO_PCS_CTRL1_CLKSTOP_EN);

		return 0;
	}
eee_exit_err:
	return -EPROTONOSUPPORT;
}
#endif

#ifdef DEBUG_EEE
static void mmd_eee_adv_to_linkmode_local(unsigned long *advertising, u16 eee_adv)
{
	linkmode_zero(advertising);

	if (eee_adv & MDIO_EEE_100TX)
		linkmode_set_bit(ETHTOOL_LINK_MODE_100baseT_Full_BIT,
				 advertising);
	if (eee_adv & MDIO_EEE_1000T)
		linkmode_set_bit(ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
				 advertising);
	if (eee_adv & MDIO_EEE_10GT)
		linkmode_set_bit(ETHTOOL_LINK_MODE_10000baseT_Full_BIT,
				 advertising);
	if (eee_adv & MDIO_EEE_1000KX)
		linkmode_set_bit(ETHTOOL_LINK_MODE_1000baseKX_Full_BIT,
				 advertising);
	if (eee_adv & MDIO_EEE_10GKX4)
		linkmode_set_bit(ETHTOOL_LINK_MODE_10000baseKX4_Full_BIT,
				 advertising);
	if (eee_adv & MDIO_EEE_10GKR)
		linkmode_set_bit(ETHTOOL_LINK_MODE_10000baseKR_Full_BIT,
				 advertising);
}

int phy_init_eee_local(struct phy_device *phydev, bool clk_stop_enable)
{
	if (!phydev->drv)
		return -EIO;

	KPRINT_INFO("----> %s\n", __func__);


	/* According to 802.3az,the EEE is supported only in full duplex-mode.
	 */
	if (phydev->duplex == DUPLEX_FULL) {
		__ETHTOOL_DECLARE_LINK_MODE_MASK(common);
		__ETHTOOL_DECLARE_LINK_MODE_MASK(lp);
		__ETHTOOL_DECLARE_LINK_MODE_MASK(adv);
		int eee_lp, eee_cap, eee_adv;
		int status;
		u32 cap;

		/* Read phy status to properly get the right settings */
		status = phy_read_status(phydev);
		if (status) {
			KPRINT_ERR("Error 0: %d\n", status);
			return status;
		}

		/* First check if the EEE ability is supported */
		eee_cap = phy_read_mmd(phydev, MDIO_MMD_PCS, MDIO_PCS_EEE_ABLE);
		if (eee_cap <= 0) {
			KPRINT_ERR("Error 1\n");
			goto eee_exit_err;
		}

		cap = mmd_eee_cap_to_ethtool_sup_t(eee_cap);
		if (!cap) {
			KPRINT_ERR("Error 2\n");
			goto eee_exit_err;
		}

		/* Check which link settings negotiated and verify it in
		 * the EEE advertising registers.
		 */
		eee_lp = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_EEE_LPABLE);
		if (eee_lp <= 0) {
			KPRINT_ERR("Error 3\n");
			goto eee_exit_err;
		}

		eee_adv = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_EEE_ADV);
		if (eee_adv <= 0) {
			KPRINT_ERR("Error 4\n");
			goto eee_exit_err;
		}

		KPRINT_INFO("%s eee_adv: 0x%x\n", __func__, eee_adv);
		KPRINT_INFO("%s eee_lp: 0x%x\n", __func__, eee_lp);


		mmd_eee_adv_to_linkmode_local(adv, eee_adv);
		mmd_eee_adv_to_linkmode_local(lp, eee_lp);

		KPRINT_INFO("%s adv: 0x%x\n", __func__, adv);
		KPRINT_INFO("%s eee_lp: 0x%x\n", __func__, lp);
		
		linkmode_and(common, adv, lp);

		KPRINT_INFO("%s common: 0x%x\n", __func__, common);

		if (!tc956x_phy_check_valid(phydev->speed, phydev->duplex, common)) {
			KPRINT_ERR("Error 5\n");
			goto eee_exit_err;
		}

		if (clk_stop_enable)
			/* Configure the PHY to stop receiving xMII
			 * clock while it is signaling LPI.
			 */
			phy_set_bits_mmd(phydev, MDIO_MMD_PCS, MDIO_CTRL1,
					 MDIO_PCS_CTRL1_CLKSTOP_EN);

		return 0; /* EEE supported */
	}
eee_exit_err:
	return -EPROTONOSUPPORT;
}
#endif

static void tc956xmac_mac_link_up(struct phylink_config *config,
			       unsigned int mode, phy_interface_t interface,
			       struct phy_device *phy)
{
	struct tc956xmac_priv *priv = netdev_priv(to_net_dev(config->dev));

	tc956xmac_mac_set_rx(priv, priv->ioaddr, true);
#ifdef EEE
	if (phy && priv->dma_cap.eee && priv->eee_enabled) {
		DBGPR_FUNC(priv->device, "%s EEE Enable, checking to enable acive\n", __func__);
#ifdef TC956X_5_G_2_5_G_EEE_SUPPORT
		if(phy->speed == TC956X_PHY_SPEED_5G || phy->speed == TC956X_PHY_SPEED_2_5G) {
			priv->eee_active = tc956x_phy_init_eee(phy, 1) >= 0;
		} else {
#ifndef DEBUG_EEE
			priv->eee_active = phy_init_eee(phy, 1) >= 0;
#else
			priv->eee_active = phy_init_eee_local(phy, 1) >= 0;
#endif
		}
#else
#ifndef DEBUG_EEE
		priv->eee_active = phy_init_eee(phy, 1) >= 0;
#else
		priv->eee_active = phy_init_eee_local(phy, 1) >= 0;
#endif
#endif
		if (priv->eee_active) {
			tc956xmac_eee_init(priv);
			tc956xmac_set_eee_pls(priv, priv->hw, true);
		}
	}
#endif
	DBGPR_FUNC(priv->device, "%s priv->eee_enabled: %d priv->eee_active: %d\n", __func__, priv->eee_enabled, priv->eee_active);

#ifdef TC956X_PM_DEBUG
	pm_generic_resume(priv->device);
#endif
}

static const struct phylink_mac_ops tc956xmac_phylink_mac_ops = {
	.validate = tc956xmac_validate,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 5, 0))
	.mac_pcs_get_state = tc956xmac_mac_pcs_get_state,
#else	/* Required when using with Kernel v5.4 */
	.mac_link_state = tc956xmac_mac_link_state,
#endif
	.mac_config = tc956xmac_mac_config,
	.mac_an_restart = tc956xmac_mac_an_restart,
	.mac_link_down = tc956xmac_mac_link_down,
	.mac_link_up = tc956xmac_mac_link_up,
};

/**
 * tc956xmac_check_pcs_mode - verify if RGMII/SGMII is supported
 * @priv: driver private structure
 * Description: this is to verify if the HW supports the PCS.
 * Physical Coding Sublayer (PCS) interface that can be used when the MAC is
 * configured for the TBI, RTBI, or SGMII PHY interface.
 */
static void tc956xmac_check_pcs_mode(struct tc956xmac_priv *priv)
{
	int interface = priv->plat->interface;

	if (priv->dma_cap.pcs) {
		if ((interface == PHY_INTERFACE_MODE_RGMII) ||
		    (interface == PHY_INTERFACE_MODE_RGMII_ID) ||
		    (interface == PHY_INTERFACE_MODE_RGMII_RXID) ||
		    (interface == PHY_INTERFACE_MODE_RGMII_TXID)) {
			netdev_dbg(priv->dev, "PCS RGMII support enabled\n");
#ifdef TC956X
			priv->hw->pcs = 0;
#else
			priv->hw->pcs = TC956XMAC_PCS_RGMII;
#endif
		} else if (interface == PHY_INTERFACE_MODE_SGMII) {
			netdev_dbg(priv->dev, "PCS SGMII support enabled\n");
#ifdef TC956X
			priv->hw->pcs = TC956XMAC_PCS_SGMII;
#endif
		} else if ((interface == PHY_INTERFACE_MODE_USXGMII) ||
			  (interface == PHY_INTERFACE_MODE_10GKR)) {
			netdev_dbg(priv->dev, "PCS USXGMII/XFI support enabled\n");
#ifdef TC956X
			priv->hw->pcs = TC956XMAC_PCS_USXGMII;
#endif
		}
	}
#ifdef TC956X
	priv->hw->pcs = 0;
	if (interface == PHY_INTERFACE_MODE_RGMII) {
		priv->hw->xpcs = 0;
	} else if (interface == PHY_INTERFACE_MODE_SGMII) {
		netdev_dbg(priv->dev, "PCS SGMII support enabled\n");
		priv->hw->xpcs = TC956XMAC_PCS_SGMII;
	} else if ((interface == PHY_INTERFACE_MODE_USXGMII) ||
		  (interface == PHY_INTERFACE_MODE_10GKR)) {
		netdev_dbg(priv->dev, "PCS USXGMII support enabled\n");
		priv->hw->xpcs = TC956XMAC_PCS_USXGMII;
	}
#endif
}

/**
 * tc956xmac_defer_phy_isr_work - Scheduled by the PHY Ext Interrupt from ISR Handler
 *  @work: work_struct
 */
static void tc956xmac_defer_phy_isr_work(struct work_struct *work)
{
	struct phy_device *phydev;
	int rd_val = 0;
	struct tc956xmac_priv *priv =
		container_of(work, struct tc956xmac_priv, emac_phy_work);
	int addr = priv->plat->phy_addr;
		
	DBGPR_FUNC(priv->device, "Entry: tc956xmac_defer_phy_isr_work\n");

	phydev = mdiobus_get_phy(priv->mii, addr);

	if (!phydev) {
		netdev_err(priv->dev, "no phy at addr %d\n", addr);
		return;
	}
	/* Call ack interrupt to clear the WOL interrupt status fields */
	if (phydev->drv->ack_interrupt)
		phydev->drv->ack_interrupt(phydev);
	
	phy_mac_interrupt(phydev);
	
	/* PHY MSI interrupt Enable */
	rd_val = readl(priv->ioaddr + TC956X_MSI_OUT_EN_OFFSET(priv->port_num)); /* MSI_OUT_EN: Reading */
	rd_val |= (1 << MSI_INT_EXT_PHY);
	writel(rd_val, priv->ioaddr + TC956X_MSI_OUT_EN_OFFSET(priv->port_num)); /* MSI_OUT_EN: Enable MAC Ext Interrupt */

	DBGPR_FUNC(priv->device, "Exit: tc956xmac_defer_phy_isr_work \n");
}

/**
 * tc956xmac_init_phy - PHY initialization
 * @dev: net device structure
 * Description: it initializes the driver's PHY state, and attaches the PHY
 * to the mac driver.
 *  Return value:
 *  0 on success
 */
static int tc956xmac_init_phy(struct net_device *dev)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
	struct device_node *node;
	int ret;
	struct phy_device *phydev;
	int addr = priv->plat->phy_addr;
	struct ethtool_eee edata;

	node = priv->plat->phylink_node;

	phydev = mdiobus_get_phy(priv->mii, addr);
  
	if (!phydev) {
		netdev_err(priv->dev, "no phy at addr %d\n", addr);
		return -ENODEV;
	}
	if(phydev->drv != NULL) {
		if (true == priv->plat->phy_interrupt_mode && (phydev->drv->config_intr)) {
			phydev->irq = PHY_IGNORE_INTERRUPT;
			phydev->interrupts =  PHY_INTERRUPT_ENABLED;
			KPRINT_INFO("PHY configured in interrupt mode \n");
			DBGPR_FUNC(priv->device, "%s PHY configured in interrupt mode\n", __func__);
			
			INIT_WORK(&priv->emac_phy_work, tc956xmac_defer_phy_isr_work);
		} else {
			phydev->irq = PHY_POLL;
			phydev->interrupts =  PHY_INTERRUPT_DISABLED;
			DBGPR_FUNC(priv->device, "%s [1] PHY configured in polling mode\n", __func__);
		}		
	} else {
		phydev->irq = PHY_POLL;
		phydev->interrupts =  PHY_INTERRUPT_DISABLED;
		DBGPR_FUNC(priv->device, "%s [2] PHY configured in polling mode\n", __func__);
	}
	
	if (node)
		ret = phylink_of_phy_connect(priv->phylink, node, 0);

	/* Some DT bindings do not set-up the PHY handle. Let's try to
	 * manually parse it
	 */
	if (!node || ret) {
		if (!phydev || (!phydev->phy_id && !phydev->is_c45)) {
			/* Try C45 */
			phydev = get_phy_device(priv->mii, addr, true);
			if (phydev && !IS_ERR(phydev)) {
				ret = phy_device_register(phydev);
				if (ret) {
					phy_device_free(phydev);
					phydev = NULL;
				}
			} else {
				phydev = NULL;
			}
		}

		if (!phydev) {
			netdev_err(priv->dev, "no phy at addr %d\n", addr);
			return -ENODEV;
		}

		ret = phylink_connect_phy(priv->phylink, phydev);

		phy_attached_info(phydev);
	}
	if (phydev->interrupts ==  PHY_INTERRUPT_ENABLED) {
		if (!(phydev->drv->config_intr &&
			!phydev->drv->config_intr(phydev))){
			KPRINT_ERR("Failed to configure PHY interrupt port number is %d", priv->port_num);
		}
	}
	/* Enable or disable EEE Advertisement based on eee_enabled settings which might be set using module param */
	edata.eee_enabled = priv->eee_enabled;
	edata.advertised = 0;

	if (priv->phylink) {
		phylink_ethtool_set_eee(priv->phylink, &edata);
	}
	return ret;
}

static int tc956xmac_phy_setup(struct tc956xmac_priv *priv)
{
	struct fwnode_handle *fwnode = of_fwnode_handle(priv->plat->phylink_node);
	int mode = priv->plat->phy_interface;
	struct phylink *phylink;

	priv->phylink_config.dev = &priv->dev->dev;
		priv->phylink_config.type = PHYLINK_NETDEV;

		phylink = phylink_create(&priv->phylink_config, fwnode,
					 mode, &tc956xmac_phylink_mac_ops);
		if (IS_ERR(phylink))
			return PTR_ERR(phylink);
	priv->phylink = phylink;
	return 0;
}

static void tc956xmac_display_rx_rings(struct tc956xmac_priv *priv)
{
	u32 rx_cnt = priv->plat->rx_queues_to_use;
	void *head_rx;
	u32 queue;

	/* Display RX rings */
	for (queue = 0; queue < rx_cnt; queue++) {
		struct tc956xmac_rx_queue *rx_q = &priv->rx_queue[queue];

		if (priv->plat->rx_dma_ch_owner[queue] != USE_IN_TC956X_SW)
			continue;

		pr_info("\tRX Queue %u rings\n", queue);

		if (priv->extend_desc)
			head_rx = (void *)rx_q->dma_erx;
		else
			head_rx = (void *)rx_q->dma_rx;

		/* Display RX ring */
		tc956xmac_display_ring(priv, head_rx, DMA_RX_SIZE, true);
	}
}

static void tc956xmac_display_tx_rings(struct tc956xmac_priv *priv)
{
	u32 tx_cnt = priv->plat->tx_queues_to_use;
	void *head_tx;
	u32 queue;

	/* Display TX rings */
	for (queue = 0; queue < tx_cnt; queue++) {
		struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[queue];

		if (priv->plat->tx_dma_ch_owner[queue] != USE_IN_TC956X_SW)
			continue;

		pr_info("\tTX Queue %d rings\n", queue);

		if (priv->extend_desc)
			head_tx = (void *)tx_q->dma_etx;
		else if (tx_q->tbs & TC956XMAC_TBS_AVAIL)
			head_tx = (void *)tx_q->dma_entx;
		else
			head_tx = (void *)tx_q->dma_tx;

		tc956xmac_display_ring(priv, head_tx, DMA_TX_SIZE, false);
	}
}

static void tc956xmac_display_rings(struct tc956xmac_priv *priv)
{
	/* Display RX ring */
	tc956xmac_display_rx_rings(priv);

	/* Display TX ring */
	tc956xmac_display_tx_rings(priv);
}

static int tc956xmac_set_bfsize(int mtu, int bufsize)
{
	int ret = bufsize;

	if (mtu >= BUF_SIZE_8KiB)
		ret = BUF_SIZE_16KiB;
	else if (mtu >= BUF_SIZE_4KiB)
		ret = BUF_SIZE_8KiB;
	else if (mtu >= BUF_SIZE_2KiB)
		ret = BUF_SIZE_4KiB;
	else if (mtu > DEFAULT_BUFSIZE)
		ret = BUF_SIZE_2KiB;
	else
		ret = DEFAULT_BUFSIZE;

	return ret;
}

/**
 * tc956xmac_clear_rx_descriptors - clear RX descriptors
 * @priv: driver private structure
 * @queue: RX queue index
 * Description: this function is called to clear the RX descriptors
 * in case of both basic and extended descriptors are used.
 */
static void tc956xmac_clear_rx_descriptors(struct tc956xmac_priv *priv, u32 queue)
{
	struct tc956xmac_rx_queue *rx_q = &priv->rx_queue[queue];
	int i;

	/* Clear the RX descriptors */
	for (i = 0; i < DMA_RX_SIZE; i++)
		if (priv->extend_desc)
			tc956xmac_init_rx_desc(priv, &rx_q->dma_erx[i].basic,
					priv->use_riwt, priv->mode,
					(i == DMA_RX_SIZE - 1),
					priv->dma_buf_sz);
		else
			tc956xmac_init_rx_desc(priv, &rx_q->dma_rx[i],
					priv->use_riwt, priv->mode,
					(i == DMA_RX_SIZE - 1),
					priv->dma_buf_sz);
}

/**
 * tc956xmac_clear_tx_descriptors - clear tx descriptors
 * @priv: driver private structure
 * @queue: TX queue index.
 * Description: this function is called to clear the TX descriptors
 * in case of both basic and extended descriptors are used.
 */
static void tc956xmac_clear_tx_descriptors(struct tc956xmac_priv *priv, u32 queue)
{
	struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[queue];
	int i;

	/* Clear the TX descriptors */
	for (i = 0; i < DMA_TX_SIZE; i++) {
		int last = (i == (DMA_TX_SIZE - 1));
		struct dma_desc *p;

		if (priv->extend_desc)
			p = &tx_q->dma_etx[i].basic;
		else if (tx_q->tbs & TC956XMAC_TBS_AVAIL)
			p = &tx_q->dma_entx[i].basic;
		else
			p = &tx_q->dma_tx[i];

		tc956xmac_init_tx_desc(priv, p, priv->mode, last);
	}
}

/**
 * tc956xmac_clear_descriptors - clear descriptors
 * @priv: driver private structure
 * Description: this function is called to clear the TX and RX descriptors
 * in case of both basic and extended descriptors are used.
 */
static void tc956xmac_clear_descriptors(struct tc956xmac_priv *priv)
{
#ifdef TC956X
	u32 rx_queue_cnt = priv->plat->rx_queues_to_use;
#endif

	u32 tx_queue_cnt = priv->plat->tx_queues_to_use;
	u32 queue;

	/* Clear the RX descriptors */
	for (queue = 0; queue < rx_queue_cnt; queue++) {
		if (priv->plat->rx_dma_ch_owner[queue] != USE_IN_TC956X_SW)
			continue;

		tc956xmac_clear_rx_descriptors(priv, queue);
	}

	/* Clear the TX descriptors */
	for (queue = 0; queue < tx_queue_cnt; queue++) {
		if (priv->plat->tx_dma_ch_owner[queue] != USE_IN_TC956X_SW)
			continue;

		tc956xmac_clear_tx_descriptors(priv, queue);
	}
}

/**
 * tc956xmac_init_rx_buffers - init the RX descriptor buffer.
 * @priv: driver private structure
 * @p: descriptor pointer
 * @i: descriptor index
 * @flags: gfp flag
 * @queue: RX queue index
 * Description: this function is called to allocate a receive buffer, perform
 * the DMA mapping and init the descriptor.
 */
static int tc956xmac_init_rx_buffers(struct tc956xmac_priv *priv, struct dma_desc *p,
				  int i, gfp_t flags, u32 queue)
{
	struct tc956xmac_rx_queue *rx_q = &priv->rx_queue[queue];
	struct tc956xmac_rx_buffer *buf = &rx_q->buf_pool[i];

	buf->page = page_pool_dev_alloc_pages(rx_q->page_pool);
	if (!buf->page)
		return -ENOMEM;

	if (priv->sph) {
		buf->sec_page = page_pool_dev_alloc_pages(rx_q->page_pool);
		if (!buf->sec_page)
			return -ENOMEM;

		buf->sec_addr = page_pool_get_dma_addr(buf->sec_page);
		tc956xmac_set_desc_sec_addr(priv, p, buf->sec_addr);
	} else {
		buf->sec_page = NULL;
	}

	buf->addr = page_pool_get_dma_addr(buf->page);
	tc956xmac_set_desc_addr(priv, p, buf->addr);
	if (priv->dma_buf_sz == BUF_SIZE_16KiB)
		tc956xmac_init_desc3(priv, p);

	return 0;
}

/**
 * tc956xmac_free_rx_buffer - free RX dma buffers
 * @priv: private structure
 * @queue: RX queue index
 * @i: buffer index.
 */
static void tc956xmac_free_rx_buffer(struct tc956xmac_priv *priv, u32 queue, int i)
{
	struct tc956xmac_rx_queue *rx_q = &priv->rx_queue[queue];
	struct tc956xmac_rx_buffer *buf = &rx_q->buf_pool[i];

	if (buf->page)
		page_pool_put_page(rx_q->page_pool, buf->page, false);
	buf->page = NULL;

	if (buf->sec_page)
		page_pool_put_page(rx_q->page_pool, buf->sec_page, false);
	buf->sec_page = NULL;
}

/**
 * tc956xmac_free_tx_buffer - free RX dma buffers
 * @priv: private structure
 * @queue: RX queue index
 * @i: buffer index.
 */
static void tc956xmac_free_tx_buffer(struct tc956xmac_priv *priv, u32 queue, int i)
{
	struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[queue];

	if (tx_q->tx_skbuff_dma) {
		if (tx_q->tx_skbuff_dma[i].buf) {
			if (tx_q->tx_skbuff_dma[i].map_as_page)
				dma_unmap_page(priv->device,
					       tx_q->tx_skbuff_dma[i].buf,
					       tx_q->tx_skbuff_dma[i].len,
					       DMA_TO_DEVICE);
			else
				dma_unmap_single(priv->device,
						 tx_q->tx_skbuff_dma[i].buf,
						 tx_q->tx_skbuff_dma[i].len,
						 DMA_TO_DEVICE);
		}
	}

	if (tx_q->tx_skbuff) {
		if (tx_q->tx_skbuff[i]) {
			dev_kfree_skb_any(tx_q->tx_skbuff[i]);
			tx_q->tx_skbuff[i] = NULL;
			tx_q->tx_skbuff_dma[i].buf = 0;
			tx_q->tx_skbuff_dma[i].map_as_page = false;
		}
	}
}

/**
 * init_dma_rx_desc_rings - init the RX descriptor rings
 * @dev: net device structure
 * @flags: gfp flag.
 * Description: this function initializes the DMA RX descriptors
 * and allocates the socket buffers. It supports the chained and ring
 * modes.
 */
static int init_dma_rx_desc_rings(struct net_device *dev, gfp_t flags)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
#ifdef TC956X
	u32 rx_count = priv->plat->rx_queues_to_use;
#endif

	int ret = -ENOMEM;
	int queue;
	int i;

	/* RX INITIALIZATION */
	netif_dbg(priv, probe, priv->dev,
		  "SKB addresses:\nskb\t\tskb data\tdma data\n");

	for (queue = 0; queue < rx_count; queue++) {
		struct tc956xmac_rx_queue *rx_q = &priv->rx_queue[queue];

		if (priv->plat->rx_dma_ch_owner[queue] != USE_IN_TC956X_SW)
			continue;

		netif_dbg(priv, probe, priv->dev,
			  "(%s) dma_rx_phy=0x%08x\n", __func__,
			  (u32)rx_q->dma_rx_phy);

		tc956xmac_clear_rx_descriptors(priv, queue);

		for (i = 0; i < DMA_RX_SIZE; i++) {
			struct dma_desc *p;

			if (priv->extend_desc)
				p = &((rx_q->dma_erx + i)->basic);
			else
				p = rx_q->dma_rx + i;

			ret = tc956xmac_init_rx_buffers(priv, p, i, flags,
						     queue);
			if (ret)
				goto err_init_rx_buffers;
		}

		rx_q->cur_rx = 0;
		rx_q->dirty_rx = (unsigned int)(i - DMA_RX_SIZE);

		/* Setup the chained descriptor addresses */
		if (priv->mode == TC956XMAC_CHAIN_MODE) {
			if (priv->extend_desc)
				tc956xmac_mode_init(priv, rx_q->dma_erx,
						rx_q->dma_rx_phy, DMA_RX_SIZE, 1);
			else
				tc956xmac_mode_init(priv, rx_q->dma_rx,
						rx_q->dma_rx_phy, DMA_RX_SIZE, 0);
		}
	}

	return 0;

err_init_rx_buffers:
	while (queue >= 0) {
		while (--i >= 0)
			tc956xmac_free_rx_buffer(priv, queue, i);

		if (queue == 0)
			break;

		i = DMA_RX_SIZE;
		queue--;
	}

	return ret;
}

/**
 * init_dma_tx_desc_rings - init the TX descriptor rings
 * @dev: net device structure.
 * Description: this function initializes the DMA TX descriptors
 * and allocates the socket buffers. It supports the chained and ring
 * modes.
 */
static int init_dma_tx_desc_rings(struct net_device *dev)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
	u32 tx_queue_cnt = priv->plat->tx_queues_to_use;
	u32 queue;
	int i;

	for (queue = 0; queue < tx_queue_cnt; queue++) {
		struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[queue];

		if (priv->plat->tx_dma_ch_owner[queue] != USE_IN_TC956X_SW)
			continue;

		netif_dbg(priv, probe, priv->dev,
			  "(%s) dma_tx_phy=0x%08x\n", __func__,
			 (u32)tx_q->dma_tx_phy);

		/* Setup the chained descriptor addresses */
		if (priv->mode == TC956XMAC_CHAIN_MODE) {
			if (priv->extend_desc)
				tc956xmac_mode_init(priv, tx_q->dma_etx,
						tx_q->dma_tx_phy, DMA_TX_SIZE, 1);
			else if (!(tx_q->tbs & TC956XMAC_TBS_AVAIL))
				tc956xmac_mode_init(priv, tx_q->dma_tx,
						tx_q->dma_tx_phy, DMA_TX_SIZE, 0);
		}

		for (i = 0; i < DMA_TX_SIZE; i++) {
			struct dma_desc *p;

			if (priv->extend_desc)
				p = &((tx_q->dma_etx + i)->basic);
			else if (tx_q->tbs & TC956XMAC_TBS_AVAIL)
				p = &((tx_q->dma_entx + i)->basic);
			else
				p = tx_q->dma_tx + i;

			tc956xmac_clear_desc(priv, p);

			tx_q->tx_skbuff_dma[i].buf = 0;
			tx_q->tx_skbuff_dma[i].map_as_page = false;
			tx_q->tx_skbuff_dma[i].len = 0;
			tx_q->tx_skbuff_dma[i].last_segment = false;
			tx_q->tx_skbuff[i] = NULL;
		}

		tx_q->dirty_tx = 0;
		tx_q->cur_tx = 0;
		tx_q->mss = 0;

		netdev_tx_reset_queue(netdev_get_tx_queue(priv->dev, queue));
	}

	return 0;
}

/**
 * init_dma_desc_rings - init the RX/TX descriptor rings
 * @dev: net device structure
 * @flags: gfp flag.
 * Description: this function initializes the DMA RX/TX descriptors
 * and allocates the socket buffers. It supports the chained and ring
 * modes.
 */
static int init_dma_desc_rings(struct net_device *dev, gfp_t flags)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
	int ret;

	ret = init_dma_rx_desc_rings(dev, flags);
	if (ret)
		return ret;

	ret = init_dma_tx_desc_rings(dev);

	tc956xmac_clear_descriptors(priv);

	if (netif_msg_hw(priv))
		tc956xmac_display_rings(priv);

	return ret;
}

/**
 * dma_free_rx_skbufs - free RX dma buffers
 * @priv: private structure
 * @queue: RX queue index
 */
static void dma_free_rx_skbufs(struct tc956xmac_priv *priv, u32 queue)
{
	int i;

	for (i = 0; i < DMA_RX_SIZE; i++)
		tc956xmac_free_rx_buffer(priv, queue, i);
}

/**
 * dma_free_tx_skbufs - free TX dma buffers
 * @priv: private structure
 * @queue: TX queue index
 */
static void dma_free_tx_skbufs(struct tc956xmac_priv *priv, u32 queue)
{
	int i;

	for (i = 0; i < DMA_TX_SIZE; i++)
		tc956xmac_free_tx_buffer(priv, queue, i);
}

/**
 * free_dma_rx_desc_resources - free RX dma desc resources
 * @priv: private structure
 */
static void free_dma_rx_desc_resources(struct tc956xmac_priv *priv)
{
#ifdef TC956X
	u32 rx_count = priv->plat->rx_queues_to_use;
#endif

	u32 queue;

	/* Free RX queue resources */
	for (queue = 0; queue < rx_count; queue++) {
		struct tc956xmac_rx_queue *rx_q = &priv->rx_queue[queue];

		if (priv->plat->rx_dma_ch_owner[queue] != USE_IN_TC956X_SW)
			continue;

		/* Release the DMA RX socket buffers */
		dma_free_rx_skbufs(priv, queue);

		/* Free DMA regions of consistent memory previously allocated */
		if (!priv->extend_desc)
			dma_free_coherent(priv->device,
					  DMA_RX_SIZE * sizeof(struct dma_desc),
					  rx_q->dma_rx, rx_q->dma_rx_phy);
		else
			dma_free_coherent(priv->device, DMA_RX_SIZE *
					  sizeof(struct dma_extended_desc),
					  rx_q->dma_erx, rx_q->dma_rx_phy);

		kfree(rx_q->buf_pool);
		if (rx_q->page_pool)
			page_pool_destroy(rx_q->page_pool);
	}
}

/**
 * free_dma_tx_desc_resources - free TX dma desc resources
 * @priv: private structure
 */
static void free_dma_tx_desc_resources(struct tc956xmac_priv *priv)
{
	u32 tx_count = priv->plat->tx_queues_to_use;
	u32 queue;

	/* Free TX queue resources */
	for (queue = 0; queue < tx_count; queue++) {
		struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[queue];
		size_t size;
		void *addr;

		if (priv->plat->tx_dma_ch_owner[queue] != USE_IN_TC956X_SW)
			continue;

		/* Release the DMA TX socket buffers */
		dma_free_tx_skbufs(priv, queue);

		if (priv->extend_desc) {
			size = sizeof(struct dma_extended_desc);
			addr = tx_q->dma_etx;
		} else if (tx_q->tbs & TC956XMAC_TBS_AVAIL) {
			size = sizeof(struct dma_edesc);
			addr = tx_q->dma_entx;
		} else {
			size = sizeof(struct dma_desc);
			addr = tx_q->dma_tx;
		}

		size *= DMA_TX_SIZE;

		dma_free_coherent(priv->device, size, addr, tx_q->dma_tx_phy);

		kfree(tx_q->tx_skbuff_dma);
		tx_q->tx_skbuff_dma = NULL;

		kfree(tx_q->tx_skbuff);
		tx_q->tx_skbuff = NULL;

	}
}

/**
 * alloc_dma_rx_desc_resources - alloc RX resources.
 * @priv: private structure
 * Description: according to which descriptor can be used (extend or basic)
 * this function allocates the resources for TX and RX paths. In case of
 * reception, for example, it pre-allocated the RX socket buffer in order to
 * allow zero-copy mechanism.
 */
static int alloc_dma_rx_desc_resources(struct tc956xmac_priv *priv)
{
#ifdef TC956X
	u32 rx_count = priv->plat->rx_queues_to_use;
#endif

	int ret = -ENOMEM;
	u32 queue;

	/* RX queues buffers and DMA */
	for (queue = 0; queue < rx_count; queue++) {
		struct tc956xmac_rx_queue *rx_q = &priv->rx_queue[queue];
		struct page_pool_params pp_params = { 0 };
		unsigned int num_pages;

		/* Create Rx DMA resources for Host owned channels only */
		if (priv->plat->rx_dma_ch_owner[queue] != USE_IN_TC956X_SW)
			continue;

		rx_q->queue_index = queue;
		rx_q->priv_data = priv;

		pp_params.flags = PP_FLAG_DMA_MAP;
		pp_params.pool_size = DMA_RX_SIZE;
		num_pages = DIV_ROUND_UP(priv->dma_buf_sz, PAGE_SIZE);
		pp_params.order = ilog2(num_pages);
		pp_params.nid = dev_to_node(priv->device);
		pp_params.dev = priv->device;
		pp_params.dma_dir = DMA_FROM_DEVICE;

		rx_q->page_pool = page_pool_create(&pp_params);
		if (IS_ERR(rx_q->page_pool)) {
			ret = PTR_ERR(rx_q->page_pool);
			rx_q->page_pool = NULL;
			goto err_dma;
		}

		rx_q->buf_pool = kcalloc(DMA_RX_SIZE, sizeof(*rx_q->buf_pool),
					 GFP_KERNEL);
		if (!rx_q->buf_pool)
			goto err_dma;

		if (priv->extend_desc) {
			rx_q->dma_erx = dma_alloc_coherent(priv->device,
							   DMA_RX_SIZE * sizeof(struct dma_extended_desc),
							   &rx_q->dma_rx_phy,
							   GFP_KERNEL);
			if (!rx_q->dma_erx)
				goto err_dma;

		} else {
			rx_q->dma_rx = dma_alloc_coherent(priv->device,
							  DMA_RX_SIZE * sizeof(struct dma_desc),
							  &rx_q->dma_rx_phy,
							  GFP_KERNEL);
			if (!rx_q->dma_rx)
				goto err_dma;
		}
	}

	return 0;

err_dma:
	free_dma_rx_desc_resources(priv);

	return ret;
}

/**
 * alloc_dma_tx_desc_resources - alloc TX resources.
 * @priv: private structure
 * Description: according to which descriptor can be used (extend or basic)
 * this function allocates the resources for TX and RX paths. In case of
 * reception, for example, it pre-allocated the RX socket buffer in order to
 * allow zero-copy mechanism.
 */
static int alloc_dma_tx_desc_resources(struct tc956xmac_priv *priv)
{
	u32 tx_count = priv->plat->tx_queues_to_use;
	int ret = -ENOMEM;
	u32 queue;

	/* TX queues buffers and DMA */
	for (queue = 0; queue < tx_count; queue++) {
		struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[queue];
		size_t size;
		void *addr;

		if (priv->plat->tx_dma_ch_owner[queue] != USE_IN_TC956X_SW)
			continue;

		tx_q->queue_index = queue;
		tx_q->priv_data = priv;

		tx_q->tx_skbuff_dma = kcalloc(DMA_TX_SIZE,
					      sizeof(*tx_q->tx_skbuff_dma),
					      GFP_KERNEL);
		if (!tx_q->tx_skbuff_dma)
			goto err_dma;

		tx_q->tx_skbuff = kcalloc(DMA_TX_SIZE,
					  sizeof(struct sk_buff *),
					  GFP_KERNEL);
		if (!tx_q->tx_skbuff)
			goto err_dma;

		if (priv->extend_desc)
			size = sizeof(struct dma_extended_desc);
		else if (tx_q->tbs & TC956XMAC_TBS_AVAIL)
			size = sizeof(struct dma_edesc);
		else
			size = sizeof(struct dma_desc);

		size *= DMA_TX_SIZE;

		addr = dma_alloc_coherent(priv->device, size,
					  &tx_q->dma_tx_phy, GFP_KERNEL);
		if (!addr)
			goto err_dma;

		if (priv->extend_desc)
			tx_q->dma_etx = addr;
		else if (tx_q->tbs & TC956XMAC_TBS_AVAIL)
			tx_q->dma_entx = addr;
		else
			tx_q->dma_tx = addr;
	}

	return 0;

err_dma:
	free_dma_tx_desc_resources(priv);
	return ret;
}

/**
 * alloc_dma_desc_resources - alloc TX/RX resources.
 * @priv: private structure
 * Description: according to which descriptor can be used (extend or basic)
 * this function allocates the resources for TX and RX paths. In case of
 * reception, for example, it pre-allocated the RX socket buffer in order to
 * allow zero-copy mechanism.
 */
static int alloc_dma_desc_resources(struct tc956xmac_priv *priv)
{
	/* RX Allocation */
	int ret = alloc_dma_rx_desc_resources(priv);

	if (ret)
		return ret;

	ret = alloc_dma_tx_desc_resources(priv);

	return ret;
}

/**
 * free_dma_desc_resources - free dma desc resources
 * @priv: private structure
 */
static void free_dma_desc_resources(struct tc956xmac_priv *priv)
{
	/* Release the DMA RX socket buffers */
	free_dma_rx_desc_resources(priv);

	/* Release the DMA TX socket buffers */
	free_dma_tx_desc_resources(priv);
}

/**
 *  tc956xmac_mac_enable_rx_queues - Enable MAC rx queues
 *  @priv: driver private structure
 *  Description: It is used for enabling the rx queues in the MAC
 */
static void tc956xmac_mac_enable_rx_queues(struct tc956xmac_priv *priv)
{
	u32 rx_queues_count = priv->plat->rx_queues_to_use;
	int queue;
	u8 mode;

	for (queue = 0; queue < rx_queues_count; queue++) {
		mode = priv->plat->rx_queues_cfg[queue].mode_to_use;
		tc956xmac_rx_queue_enable(priv, priv->hw, mode, queue);
	}
}

/**
 * tc956xmac_start_rx_dma - start RX DMA channel
 * @priv: driver private structure
 * @chan: RX channel index
 * Description:
 * This starts a RX DMA channel
 */
static void tc956xmac_start_rx_dma(struct tc956xmac_priv *priv, u32 chan)
{
	netdev_dbg(priv->dev, "DMA RX processes started in channel %d\n", chan);
	tc956xmac_start_rx(priv, priv->ioaddr, chan);
}

/**
 * tc956xmac_start_tx_dma - start TX DMA channel
 * @priv: driver private structure
 * @chan: TX channel index
 * Description:
 * This starts a TX DMA channel
 */
static void tc956xmac_start_tx_dma(struct tc956xmac_priv *priv, u32 chan)
{
	netdev_dbg(priv->dev, "DMA TX processes started in channel %d\n", chan);
	tc956xmac_start_tx(priv, priv->ioaddr, chan);
}

/**
 * tc956xmac_stop_rx_dma - stop RX DMA channel
 * @priv: driver private structure
 * @chan: RX channel index
 * Description:
 * This stops a RX DMA channel
 */
static void tc956xmac_stop_rx_dma(struct tc956xmac_priv *priv, u32 chan)
{
	netdev_dbg(priv->dev, "DMA RX processes stopped in channel %d\n", chan);
	tc956xmac_stop_rx(priv, priv->ioaddr, chan);
}

/**
 * tc956xmac_stop_tx_dma - stop TX DMA channel
 * @priv: driver private structure
 * @chan: TX channel index
 * Description:
 * This stops a TX DMA channel
 */
static void tc956xmac_stop_tx_dma(struct tc956xmac_priv *priv, u32 chan)
{
	netdev_dbg(priv->dev, "DMA TX processes stopped in channel %d\n", chan);
	tc956xmac_stop_tx(priv, priv->ioaddr, chan);
}

/**
 * tc956xmac_start_all_dma - start all RX and TX DMA channels
 * @priv: driver private structure
 * Description:
 * This starts all the RX and TX DMA channels
 */
static void tc956xmac_start_all_dma(struct tc956xmac_priv *priv)
{
#ifdef TC956X
	u32 rx_channels_count = priv->plat->rx_queues_to_use;
#endif

	u32 tx_channels_count = priv->plat->tx_queues_to_use;
	u32 chan = 0;

	for (chan = 0; chan < rx_channels_count; chan++) {
		if (priv->plat->rx_dma_ch_owner[chan] != USE_IN_TC956X_SW)
			continue;
		tc956xmac_start_rx_dma(priv, chan);
	}

	for (chan = 0; chan < tx_channels_count; chan++) {
		if (priv->plat->tx_dma_ch_owner[chan] != USE_IN_TC956X_SW)
			continue;
		tc956xmac_start_tx_dma(priv, chan);
	}
}

/**
 * tc956xmac_stop_all_dma - stop all RX and TX DMA channels
 * @priv: driver private structure
 * Description:
 * This stops the RX and TX DMA channels
 */
static void tc956xmac_stop_all_dma(struct tc956xmac_priv *priv)
{
#ifdef TC956X
	u32 rx_channels_count = priv->plat->rx_queues_to_use;
#endif

	u32 tx_channels_count = priv->plat->tx_queues_to_use;
	u32 chan = 0;

	for (chan = 0; chan < rx_channels_count; chan++) {
		if (priv->plat->rx_dma_ch_owner[chan] != USE_IN_TC956X_SW)
			continue;
		tc956xmac_stop_rx_dma(priv, chan);
	}

	for (chan = 0; chan < tx_channels_count; chan++) {
		if (priv->plat->tx_dma_ch_owner[chan] != USE_IN_TC956X_SW)
			continue;
		tc956xmac_stop_tx_dma(priv, chan);
	}
}

/**
 *  tc956xmac_dma_operation_mode - HW DMA operation mode
 *  @priv: driver private structure
 *  Description: it is used for configuring the DMA operation mode register in
 *  order to program the tx/rx DMA thresholds or Store-And-Forward mode.
 */
static void tc956xmac_dma_operation_mode(struct tc956xmac_priv *priv)
{
	u32 rx_channels_count = priv->plat->rx_queues_to_use;
	u32 tx_channels_count = priv->plat->tx_queues_to_use;
	int rxfifosz = priv->plat->rx_fifo_size;
	int txfifosz = priv->plat->tx_fifo_size;
	u32 txmode = 0;
	u32 rxmode = 0;
	u32 chan = 0;
	u8 qmode = 0;

	if (rxfifosz == 0)
		rxfifosz = priv->dma_cap.rx_fifo_size;
	if (txfifosz == 0)
		txfifosz = priv->dma_cap.tx_fifo_size;

	/* Adjust for real per queue fifo size */
	rxfifosz /= rx_channels_count;
	txfifosz /= tx_channels_count;

	if (priv->plat->force_thresh_dma_mode) {
		txmode = tc;
		rxmode = tc;
	} else if (priv->plat->force_sf_dma_mode || priv->plat->tx_coe) {
		/*
		 * In case of GMAC, SF mode can be enabled
		 * to perform the TX COE in HW. This depends on:
		 * 1) TX COE if actually supported
		 * 2) There is no bugged Jumbo frame support
		 *    that needs to not insert csum in the TDES.
		 */
		txmode = SF_DMA_MODE;
		rxmode = SF_DMA_MODE;
		priv->xstats.threshold = SF_DMA_MODE;
	} else {
		txmode = tc;
		rxmode = SF_DMA_MODE;
	}

	/* configure all channels */
	for (chan = 0; chan < rx_channels_count; chan++) {
#ifdef TC956X
		switch (chan) {
		case 0:
			rxfifosz = priv->plat->rx_queues_cfg[0].size;
			break;
		case 1:
			rxfifosz = priv->plat->rx_queues_cfg[1].size;
			break;
		case 2:
			rxfifosz = RX_QUEUE2_SIZE;
			break;
		case 3:
			rxfifosz = RX_QUEUE3_SIZE;
			break;
		case 4:
			rxfifosz = RX_QUEUE4_SIZE;
			break;
		case 5:
			rxfifosz = RX_QUEUE5_SIZE;
			break;
		case 6:
			rxfifosz = RX_QUEUE6_SIZE;
			break;
		case 7:
			rxfifosz = RX_QUEUE7_SIZE;
			break;
		default:
			rxfifosz = RX_QUEUE0_SIZE;
			break;
		}
#endif

		qmode = priv->plat->rx_queues_cfg[chan].mode_to_use;

		tc956xmac_dma_rx_mode(priv, priv->ioaddr, rxmode, chan,
				rxfifosz, qmode);
#ifdef TC956X
		if (priv->plat->rx_dma_ch_owner[chan] == USE_IN_TC956X_SW)
			tc956xmac_set_dma_bfsize(priv, priv->ioaddr, priv->dma_buf_sz,
						chan);
#endif

	}

	for (chan = 0; chan < tx_channels_count; chan++) {
#ifdef TC956X
		switch (chan) {
		case 0:
			txfifosz = priv->plat->tx_queues_cfg[0].size;
			break;
		case 1:
			txfifosz = priv->plat->tx_queues_cfg[1].size;
			break;
		case 2:
			txfifosz = TX_QUEUE2_SIZE;
			break;
		case 3:
			txfifosz = TX_QUEUE3_SIZE;
			break;
		case 4:
			txfifosz = TX_QUEUE4_SIZE;
			break;
		case 5:
			txfifosz = TX_QUEUE5_SIZE;
			break;
		case 6:
			txfifosz = TX_QUEUE6_SIZE;
			break;
		case 7:
			txfifosz = TX_QUEUE7_SIZE;
			break;
		default:
			txfifosz = TX_QUEUE0_SIZE;
			break;
		}
#endif

		qmode = priv->plat->tx_queues_cfg[chan].mode_to_use;

		tc956xmac_dma_tx_mode(priv, priv->ioaddr, txmode, chan,
				txfifosz, qmode);
	}
}

/**
 * tc956xmac_tx_clean - to manage the transmission completion
 * @priv: driver private structure
 * @queue: TX queue index
 * Description: it reclaims the transmit resources after transmission completes.
 */
static int tc956xmac_tx_clean(struct tc956xmac_priv *priv, int budget, u32 queue)
{
	struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[queue];
	unsigned int bytes_compl = 0, pkts_compl = 0;
	unsigned int entry, count = 0;

	__netif_tx_lock_bh(netdev_get_tx_queue(priv->dev, queue));

	priv->xstats.tx_clean[queue]++;

	entry = tx_q->dirty_tx;
	while ((entry != tx_q->cur_tx) && (count < budget)) {
		struct sk_buff *skb = tx_q->tx_skbuff[entry];
		struct dma_desc *p;
		int status;

		if (priv->extend_desc)
			p = (struct dma_desc *)(tx_q->dma_etx + entry);
		else if (tx_q->tbs & TC956XMAC_TBS_AVAIL)
			p = &tx_q->dma_entx[entry].basic;
		else
			p = tx_q->dma_tx + entry;

		status = tc956xmac_tx_status(priv, &priv->dev->stats,
				&priv->xstats, p, priv->ioaddr);
		/* Check if the descriptor is owned by the DMA */
		if (unlikely(status & tx_dma_own))
			break;

		count++;

		/* Make sure descriptor fields are read after reading
		 * the own bit.
		 */
		dma_rmb();

		/* Just consider the last segment and ...*/
		if (likely(!(status & tx_not_ls))) {
			/* ... verify the status error condition */
			if (unlikely(status & tx_err)) {
				priv->dev->stats.tx_errors++;
				priv->xstats.tx_pkt_errors_n[queue]++;
			} else {
				priv->dev->stats.tx_packets++;
				priv->xstats.tx_pkt_n[queue]++;
			}
#if defined(TX_LOGGING_TRACE)
			if (queue != skb_get_queue_mapping(skb))
				pr_err("Tx Queue no. is different\n");
#endif
			tc956xmac_get_tx_hwtstamp(priv, p, skb);
		}

		if (likely(tx_q->tx_skbuff_dma[entry].buf)) {
			if (tx_q->tx_skbuff_dma[entry].map_as_page)
				dma_unmap_page(priv->device,
					       tx_q->tx_skbuff_dma[entry].buf,
					       tx_q->tx_skbuff_dma[entry].len,
					       DMA_TO_DEVICE);
			else
				dma_unmap_single(priv->device,
						 tx_q->tx_skbuff_dma[entry].buf,
						 tx_q->tx_skbuff_dma[entry].len,
						 DMA_TO_DEVICE);
			tx_q->tx_skbuff_dma[entry].buf = 0;
			tx_q->tx_skbuff_dma[entry].len = 0;
			tx_q->tx_skbuff_dma[entry].map_as_page = false;
		}

		tc956xmac_clean_desc3(priv, tx_q, p);

		tx_q->tx_skbuff_dma[entry].last_segment = false;
		tx_q->tx_skbuff_dma[entry].is_jumbo = false;

		if (likely(skb != NULL)) {
			pkts_compl++;
			bytes_compl += skb->len;
			dev_consume_skb_any(skb);
			tx_q->tx_skbuff[entry] = NULL;
		}

		tc956xmac_release_tx_desc(priv, p, priv->mode);

		entry = TC956XMAC_GET_ENTRY(entry, DMA_TX_SIZE);
	}
	tx_q->dirty_tx = entry;

	netdev_tx_completed_queue(netdev_get_tx_queue(priv->dev, queue),
				  pkts_compl, bytes_compl);

	if (unlikely(netif_tx_queue_stopped(netdev_get_tx_queue(priv->dev, queue))) &&
	    tc956xmac_tx_avail(priv, queue) > TC956XMAC_TX_THRESH) {

		netif_dbg(priv, tx_done, priv->dev,
			  "%s: restart transmit\n", __func__);
		netif_tx_wake_queue(netdev_get_tx_queue(priv->dev, queue));
	}
#ifdef ENABLE_TX_TIMER
	/* We still have pending packets, let's call for a new scheduling */
	if (tx_q->dirty_tx != tx_q->cur_tx)
		mod_timer(&tx_q->txtimer, TC956XMAC_COAL_TIMER(priv->tx_coal_timer));
#endif
	__netif_tx_unlock_bh(netdev_get_tx_queue(priv->dev, queue));

	return count;
}

/**
 * tc956xmac_tx_err - to manage the tx error
 * @priv: driver private structure
 * @chan: channel index
 * Description: it cleans the descriptors and restarts the transmission
 * in case of transmission errors.
 */
static void tc956xmac_tx_err(struct tc956xmac_priv *priv, u32 chan)
{
	struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[chan];

	netif_tx_stop_queue(netdev_get_tx_queue(priv->dev, chan));

	tc956xmac_stop_tx_dma(priv, chan);
	dma_free_tx_skbufs(priv, chan);
	tc956xmac_clear_tx_descriptors(priv, chan);
	tx_q->dirty_tx = 0;
	tx_q->cur_tx = 0;
	tx_q->mss = 0;
	netdev_tx_reset_queue(netdev_get_tx_queue(priv->dev, chan));
	tc956xmac_init_tx_chan(priv, priv->ioaddr, priv->plat->dma_cfg,
			    tx_q->dma_tx_phy, chan);
	tc956xmac_start_tx_dma(priv, chan);

	priv->dev->stats.tx_errors++;
	netif_tx_wake_queue(netdev_get_tx_queue(priv->dev, chan));
}

/**
 *  tc956xmac_set_dma_operation_mode - Set DMA operation mode by channel
 *  @priv: driver private structure
 *  @txmode: TX operating mode
 *  @rxmode: RX operating mode
 *  @chan: channel index
 *  Description: it is used for configuring of the DMA operation mode in
 *  runtime in order to program the tx/rx DMA thresholds or Store-And-Forward
 *  mode.
 */
static void tc956xmac_set_dma_operation_mode(struct tc956xmac_priv *priv, u32 txmode,
					  u32 rxmode, u32 chan)
{
	u8 rxqmode = priv->plat->rx_queues_cfg[chan].mode_to_use;
	u8 txqmode = priv->plat->tx_queues_cfg[chan].mode_to_use;
	u32 rx_channels_count = priv->plat->rx_queues_to_use;
	u32 tx_channels_count = priv->plat->tx_queues_to_use;
	int rxfifosz = priv->plat->rx_fifo_size;
	int txfifosz = priv->plat->tx_fifo_size;

	if (rxfifosz == 0)
		rxfifosz = priv->dma_cap.rx_fifo_size;
	if (txfifosz == 0)
		txfifosz = priv->dma_cap.tx_fifo_size;

	/* Adjust for real per queue fifo size */
	rxfifosz /= rx_channels_count;
	txfifosz /= tx_channels_count;

#ifdef TC956X
	switch (chan) {
	case 0:
		rxfifosz = priv->plat->rx_queues_cfg[0].size;
		txfifosz = priv->plat->tx_queues_cfg[0].size;
		break;
	case 1:
		rxfifosz = priv->plat->rx_queues_cfg[1].size;
		txfifosz = priv->plat->tx_queues_cfg[1].size;
		break;
	case 2:
		rxfifosz = RX_QUEUE2_SIZE;
		txfifosz = TX_QUEUE2_SIZE;
		break;
	case 3:
		rxfifosz = RX_QUEUE3_SIZE;
		txfifosz = TX_QUEUE3_SIZE;
		break;
	case 4:
		rxfifosz = RX_QUEUE4_SIZE;
		txfifosz = TX_QUEUE4_SIZE;
		break;
	case 5:
		rxfifosz = RX_QUEUE5_SIZE;
		txfifosz = TX_QUEUE5_SIZE;
		break;
	case 6:
		rxfifosz = RX_QUEUE6_SIZE;
		txfifosz = TX_QUEUE6_SIZE;
		break;
	case 7:
		rxfifosz = RX_QUEUE7_SIZE;
		txfifosz = TX_QUEUE7_SIZE;
		break;
	default:
		rxfifosz = RX_QUEUE0_SIZE;
		txfifosz = TX_QUEUE0_SIZE;
		break;
	}
#endif

	tc956xmac_dma_rx_mode(priv, priv->ioaddr, rxmode, chan, rxfifosz, rxqmode);
	tc956xmac_dma_tx_mode(priv, priv->ioaddr, txmode, chan, txfifosz, txqmode);
}

static bool tc956xmac_safety_feat_interrupt(struct tc956xmac_priv *priv)
{
	int ret;

	ret = tc956xmac_safety_feat_irq_status(priv, priv->dev,
			priv->ioaddr, priv->dma_cap.asp, &priv->sstats);
	if (ret && (ret != -EINVAL)) {
		tc956xmac_global_err(priv);
		return true;
	}

	return false;
}

static int tc956xmac_napi_check(struct tc956xmac_priv *priv, u32 chan)
{
	int status = tc956xmac_dma_interrupt_status(priv, priv->ioaddr,
						 &priv->xstats, chan);
	struct tc956xmac_channel *ch = &priv->channel[chan];
	unsigned long flags;

#ifdef TC956X
	if ((status & handle_rx) && (chan < priv->plat->rx_queues_to_use) &&
		(priv->plat->rx_dma_ch_owner[chan] == USE_IN_TC956X_SW)) {
#endif

		if (napi_schedule_prep(&ch->rx_napi)) {
			spin_lock_irqsave(&ch->lock, flags);
			tc956xmac_disable_dma_irq(priv, priv->ioaddr, chan, 1, 0);
			spin_unlock_irqrestore(&ch->lock, flags);
			__napi_schedule_irqoff(&ch->rx_napi);
		}
	}

	if ((status & handle_tx) && (chan < priv->plat->tx_queues_to_use) &&
		(priv->plat->tx_dma_ch_owner[chan] == USE_IN_TC956X_SW)) {

#ifdef TX_COMPLETION_WITHOUT_TIMERS
		writel(0, priv->tc956x_SRAM_pci_base_addr
				+ TX_TIMER_SRAM_OFFSET(priv->port_num));

#endif
		if (napi_schedule_prep(&ch->tx_napi)) {
			spin_lock_irqsave(&ch->lock, flags);
			tc956xmac_disable_dma_irq(priv, priv->ioaddr, chan, 0, 1);
			spin_unlock_irqrestore(&ch->lock, flags);
			__napi_schedule_irqoff(&ch->tx_napi);
		}
	}

	return status;
}

/**
 * tc956xmac_dma_interrupt - DMA ISR
 * @priv: driver private structure
 * Description: this is the DMA ISR. It is called by the main ISR.
 * It calls the dwmac dma routine and schedule poll method in case of some
 * work can be done.
 */
static void tc956xmac_dma_interrupt(struct tc956xmac_priv *priv)
{
	u32 tx_channel_count = priv->plat->tx_queues_to_use;

#ifdef TC956X
	u32 rx_channel_count = priv->plat->rx_queues_to_use;
#endif
	u32 channels_to_check = tx_channel_count > rx_channel_count ?
				tx_channel_count : rx_channel_count;
	u32 chan;
	int status[max_t(u32, MTL_MAX_TX_QUEUES, MTL_MAX_RX_QUEUES)];

	/* Make sure we never check beyond our status buffer. */
	if (WARN_ON_ONCE(channels_to_check > ARRAY_SIZE(status)))
		channels_to_check = ARRAY_SIZE(status);

	for (chan = 0; chan < channels_to_check; chan++)
		status[chan] = tc956xmac_napi_check(priv, chan);

	for (chan = 0; chan < tx_channel_count; chan++) {
		if (unlikely(status[chan] & tx_hard_error_bump_tc)) {
			/* Try to bump up the dma threshold on this failure */
			if (unlikely(priv->xstats.threshold != SF_DMA_MODE) &&
			    (tc <= 256)) {
				tc += 64;
				if (priv->plat->force_thresh_dma_mode)
					tc956xmac_set_dma_operation_mode(priv,
								      tc,
								      tc,
								      chan);
				else
					tc956xmac_set_dma_operation_mode(priv,
								    tc,
								    SF_DMA_MODE,
								    chan);
				priv->xstats.threshold = tc;
			}
		} else if (unlikely(status[chan] == tx_hard_error)) {
			tc956xmac_tx_err(priv, chan);
		}
	}
}

/**
 * tc956xmac_mmc_setup: setup the Mac Management Counters (MMC)
 * @priv: driver private structure
 * Description: this masks the MMC irq, in fact, the counters are managed in SW.
 */
static void tc956xmac_mmc_setup(struct tc956xmac_priv *priv)
{
	unsigned int mode = MMC_CNTRL_RESET_ON_READ | MMC_CNTRL_COUNTER_RESET |
			    MMC_CNTRL_PRESET | MMC_CNTRL_FULL_HALF_PRESET;

	tc956xmac_mmc_intr_all_mask(priv, priv->mmcaddr);

	if (priv->dma_cap.rmon) {
		tc956xmac_mmc_ctrl(priv, priv->mmcaddr, mode);
		memset(&priv->mmc, 0, sizeof(struct tc956xmac_counters));
	} else
		netdev_info(priv->dev, "No MAC Management Counters available\n");
}

/**
 * tc956xmac_get_hw_features - get MAC capabilities from the HW cap. register.
 * @priv: driver private structure
 * Description:
 *  new GMAC chip generations have a new register to indicate the
 *  presence of the optional feature/functions.
 *  This can be also used to override the value passed through the
 *  platform and necessary for old MAC10/100 and GMAC chips.
 */
static int tc956xmac_get_hw_features(struct tc956xmac_priv *priv)
{
	return tc956xmac_get_hw_feature(priv, priv->ioaddr, &priv->dma_cap) == 0;
}

/**
 * tc956xmac_check_ether_addr - check if the MAC addr is valid
 * @priv: driver private structure
 * Description:
 * it is to verify if the MAC address is valid, in case of failures it
 * generates a random MAC address
 */
static void tc956xmac_check_ether_addr(struct tc956xmac_priv *priv)
{
	if (!is_valid_ether_addr(priv->dev->dev_addr)) {
		tc956xmac_get_umac_addr(priv, priv->hw, priv->dev->dev_addr, 0);
		if (!is_valid_ether_addr(priv->dev->dev_addr))
			eth_hw_addr_random(priv->dev);
		dev_info(priv->device, "device MAC address %pM\n",
			 priv->dev->dev_addr);
	}
}

/**
 * tc956xmac_init_dma_engine - DMA init.
 * @priv: driver private structure
 * Description:
 * It inits the DMA invoking the specific MAC/GMAC callback.
 * Some DMA parameters can be passed from the platform;
 * in case of these are not passed a default is kept for the MAC or GMAC.
 */
static int tc956xmac_init_dma_engine(struct tc956xmac_priv *priv)
{
#ifdef TC956X
	u32 rx_channels_count = priv->plat->rx_queues_to_use;
#endif

	u32 tx_channels_count = priv->plat->tx_queues_to_use;
	u32 dma_csr_ch = max(rx_channels_count, tx_channels_count);
	struct tc956xmac_rx_queue *rx_q;
	struct tc956xmac_tx_queue *tx_q;
	u32 chan = 0;
	int atds = 0;
	int ret = 0;

	if (!priv->plat->dma_cfg || !priv->plat->dma_cfg->txpbl ||
	    !priv->plat->dma_cfg->rxpbl) {
		dev_err(priv->device, "Invalid DMA configuration\n");
		return -EINVAL;
	}

	if (priv->extend_desc && (priv->mode == TC956XMAC_RING_MODE))
		atds = 1;

	ret = tc956xmac_reset(priv, priv->ioaddr);
	if (ret) {
		dev_err(priv->device, "Failed to reset the dma\n");
		return ret;
	}

	/* DMA Configuration */
	tc956xmac_dma_init(priv, priv->ioaddr, priv->plat->dma_cfg, atds);

	if (priv->plat->axi)
		tc956xmac_axi(priv, priv->ioaddr, priv->plat->axi);

	/* DMA CSR Channel configuration */
	for (chan = 0; chan < dma_csr_ch; chan++)
		tc956xmac_init_chan(priv, priv->ioaddr, priv->plat->dma_cfg, chan);

	/* DMA RX Channel Configuration */
	for (chan = 0; chan < rx_channels_count; chan++) {
		if (priv->plat->rx_dma_ch_owner[chan] != USE_IN_TC956X_SW)
			continue;

		rx_q = &priv->rx_queue[chan];

		tc956xmac_init_rx_chan(priv, priv->ioaddr, priv->plat->dma_cfg, rx_q->dma_rx_phy, chan);

		rx_q->rx_tail_addr = rx_q->dma_rx_phy +
			    (DMA_RX_SIZE * sizeof(struct dma_desc));
		tc956xmac_set_rx_tail_ptr(priv, priv->ioaddr, rx_q->rx_tail_addr, chan);
	}

	/* DMA TX Channel Configuration */
	for (chan = 0; chan < tx_channels_count; chan++) {
		if (priv->plat->tx_dma_ch_owner[chan] != USE_IN_TC956X_SW)
			continue;

		tx_q = &priv->tx_queue[chan];

		tc956xmac_init_tx_chan(priv, priv->ioaddr, priv->plat->dma_cfg,
				    tx_q->dma_tx_phy, chan);

		tx_q->tx_tail_addr = tx_q->dma_tx_phy;
		tc956xmac_set_tx_tail_ptr(priv, priv->ioaddr,
				       tx_q->tx_tail_addr, chan);
	}

	return ret;
}

#ifdef ENABLE_TX_TIMER
static void tc956xmac_tx_timer_arm(struct tc956xmac_priv *priv, u32 queue)
{
	struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[queue];

	mod_timer(&tx_q->txtimer, TC956XMAC_COAL_TIMER(priv->tx_coal_timer));
}

/**
 * tc956xmac_tx_timer - mitigation sw timer for tx.
 * @data: data pointer
 * Description:
 * This is the timer handler to directly invoke the tc956xmac_tx_clean.
 */
static void tc956xmac_tx_timer(struct timer_list *t)
{
	struct tc956xmac_tx_queue *tx_q = from_timer(tx_q, t, txtimer);
	struct tc956xmac_priv *priv = tx_q->priv_data;
	struct tc956xmac_channel *ch;

	ch = &priv->channel[tx_q->queue_index];

	if (priv->plat->tx_dma_ch_owner[tx_q->queue_index] != USE_IN_TC956X_SW)
		return;

	/*
	 * If NAPI is already running we can miss some events. Let's rearm
	 * the timer and try again.
	 */
	if (likely(napi_schedule_prep(&ch->tx_napi))) {
		unsigned long flags;

		spin_lock_irqsave(&ch->lock, flags);
		tc956xmac_disable_dma_irq(priv, priv->ioaddr, ch->index, 0, 1);
		spin_unlock_irqrestore(&ch->lock, flags);
		__napi_schedule(&ch->tx_napi);
	}
}

#endif

/**
 * tc956xmac_init_coalesce - init mitigation options.
 * @priv: driver private structure
 * Description:
 * This inits the coalesce parameters: i.e. timer rate,
 * timer handler and default threshold used for enabling the
 * interrupt on completion bit.
 */
static void tc956xmac_init_coalesce(struct tc956xmac_priv *priv)
{
#ifdef ENABLE_TX_TIMER
	u32 tx_channel_count = priv->plat->tx_queues_to_use;
	u32 chan;
#endif
	priv->tx_coal_frames = TC956XMAC_TX_FRAMES;
	priv->tx_coal_timer = TC956XMAC_COAL_TX_TIMER;
	priv->rx_coal_frames = TC956XMAC_RX_FRAMES;

#ifdef ENABLE_TX_TIMER
	for (chan = 0; chan < tx_channel_count; chan++) {
		struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[chan];

		if (priv->plat->tx_dma_ch_owner[chan] != USE_IN_TC956X_SW)
			continue;
		timer_setup(&tx_q->txtimer, tc956xmac_tx_timer, 0);
	}
#endif
}

static void tc956xmac_set_rings_length(struct tc956xmac_priv *priv)
{
#ifdef TC956X
	u32 rx_channels_count = priv->plat->rx_queues_to_use;
#endif

	u32 tx_channels_count = priv->plat->tx_queues_to_use;
	u32 chan;

	/* set TX ring length */
	for (chan = 0; chan < tx_channels_count; chan++) {
		if (priv->plat->tx_dma_ch_owner[chan] != USE_IN_TC956X_SW)
			continue;

		tc956xmac_set_tx_ring_len(priv, priv->ioaddr,
				(DMA_TX_SIZE - 1), chan);
	}

	/* set RX ring length */
	for (chan = 0; chan < rx_channels_count; chan++) {
		if (priv->plat->rx_dma_ch_owner[chan] != USE_IN_TC956X_SW)
			continue;

		tc956xmac_set_rx_ring_len(priv, priv->ioaddr,
				(DMA_RX_SIZE - 1), chan);
	}
}

/**
 *  tc956xmac_set_tx_queue_weight - Set TX queue weight
 *  @priv: driver private structure
 *  Description: It is used for setting TX queues weight
 */
static void tc956xmac_set_tx_queue_weight(struct tc956xmac_priv *priv)
{
	u32 tx_queues_count = priv->plat->tx_queues_to_use;
	u32 weight;
	u32 queue;

	for (queue = 0; queue < tx_queues_count; queue++) {
		weight = priv->plat->tx_queues_cfg[queue].weight;
		tc956xmac_set_mtl_tx_queue_weight(priv, priv->hw, weight, queue);
	}
}

/**
 *  tc956xmac_configure_cbs - Configure CBS in TX queue
 *  @priv: driver private structure
 *  Description: It is used for configuring CBS in AVB TX queues
 */
static void tc956xmac_configure_cbs(struct tc956xmac_priv *priv)
{
	u32 tx_queues_count = priv->plat->tx_queues_to_use;
	u32 mode_to_use;
	u32 queue;

	/* queue 0 is reserved for legacy traffic */
	for (queue = 1; queue < tx_queues_count; queue++) {
		mode_to_use = priv->plat->tx_queues_cfg[queue].mode_to_use;
		if (mode_to_use != MTL_QUEUE_AVB)
			continue;

#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
		tc956xmac_config_cbs(priv, priv->hw,
				priv->plat->tx_queues_cfg[queue].send_slope,
				priv->plat->tx_queues_cfg[queue].idle_slope,
				priv->plat->tx_queues_cfg[queue].high_credit,
				priv->plat->tx_queues_cfg[queue].low_credit,
				queue);
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */
	}
}

/**
 *  tc956xmac_rx_queue_dma_chan_map - Map RX queue to RX dma channel
 *  @priv: driver private structure
 *  Description: It is used for mapping RX queues to RX dma channels
 */
static void tc956xmac_rx_queue_dma_chan_map(struct tc956xmac_priv *priv)
{
	u32 rx_queues_count = priv->plat->rx_queues_to_use;
	u32 queue;
	u32 chan;

	for (queue = 0; queue < rx_queues_count; queue++) {
		chan = priv->plat->rx_queues_cfg[queue].chan;
		tc956xmac_map_mtl_to_dma(priv, priv->hw, queue, chan);
	}
}

/**
 *  tc956xmac_mac_config_rx_queues_prio - Configure RX Queue priority
 *  @priv: driver private structure
 *  Description: It is used for configuring the RX Queue Priority
 */
static void tc956xmac_mac_config_rx_queues_prio(struct tc956xmac_priv *priv)
{
	u32 rx_queues_count = priv->plat->rx_queues_to_use;
	u32 queue;
	u32 prio;

	for (queue = 0; queue < rx_queues_count; queue++) {
		if (!priv->plat->rx_queues_cfg[queue].use_prio)
			continue;

		prio = priv->plat->rx_queues_cfg[queue].prio;
		tc956xmac_rx_queue_prio(priv, priv->hw, prio, queue);
	}
}

/**
 *  tc956xmac_mac_config_tx_queues_prio - Configure TX Queue priority
 *  @priv: driver private structure
 *  Description: It is used for configuring the TX Queue Priority
 */
static void tc956xmac_mac_config_tx_queues_prio(struct tc956xmac_priv *priv)
{
	u32 tx_queues_count = priv->plat->tx_queues_to_use;
	u32 queue;
	u32 prio;

	for (queue = 0; queue < tx_queues_count; queue++) {
		if (!priv->plat->tx_queues_cfg[queue].use_prio)
			continue;

		prio = priv->plat->tx_queues_cfg[queue].prio;
		tc956xmac_tx_queue_prio(priv, priv->hw, prio, queue);
	}
}

/**
 *  tc956xmac_mac_config_rx_queues_routing - Configure RX Queue Routing
 *  @priv: driver private structure
 *  Description: It is used for configuring the RX queue routing
 */
static void tc956xmac_mac_config_rx_queues_routing(struct tc956xmac_priv *priv)
{
	u32 rx_queues_count = priv->plat->rx_queues_to_use;
	u32 queue;
	u8 packet;

	for (queue = 0; queue < rx_queues_count; queue++) {
		/* no specific packet type routing specified for the queue */
		if (priv->plat->rx_queues_cfg[queue].pkt_route == 0x0)
			continue;

		packet = priv->plat->rx_queues_cfg[queue].pkt_route;
		tc956xmac_rx_queue_routing(priv, priv->hw, packet, queue);
	}
}

static void tc956xmac_mac_config_rss(struct tc956xmac_priv *priv)
{
	if (!priv->dma_cap.rssen || !priv->plat->rss_en) {
		priv->rss.enable = false;
		return;
	}

	if (priv->dev->features & NETIF_F_RXHASH)
		priv->rss.enable = true;
	else
		priv->rss.enable = false;
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
	tc956xmac_rss_configure(priv, priv->hw, &priv->rss,
			     priv->plat->rx_queues_to_use);
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */
}

/**
 *  tc956xmac_mtl_configuration - Configure MTL
 *  @priv: driver private structure
 *  Description: It is used for configurring MTL
 */
static void tc956xmac_mtl_configuration(struct tc956xmac_priv *priv)
{
	u32 rx_queues_count = priv->plat->rx_queues_to_use;
	u32 tx_queues_count = priv->plat->tx_queues_to_use;

	if (tx_queues_count > 1)
		tc956xmac_set_tx_queue_weight(priv);

	/* Configure MTL RX algorithms */
	if (rx_queues_count > 1)
		tc956xmac_prog_mtl_rx_algorithms(priv, priv->hw,
				priv->plat->rx_sched_algorithm);

	/* Configure MTL TX algorithms */
	if (tx_queues_count > 1)
		tc956xmac_prog_mtl_tx_algorithms(priv, priv->hw,
				priv->plat->tx_sched_algorithm);

	/* Configure CBS in AVB TX queues */
	if (tx_queues_count > 1)
		tc956xmac_configure_cbs(priv);

	/* Map RX MTL to DMA channels */
	tc956xmac_rx_queue_dma_chan_map(priv);

	/* Enable MAC RX Queues */
	tc956xmac_mac_enable_rx_queues(priv);

	/* Set RX priorities */
	if (rx_queues_count > 1)
		tc956xmac_mac_config_rx_queues_prio(priv);

	/* Set TX priorities */
	if (tx_queues_count > 1)
		tc956xmac_mac_config_tx_queues_prio(priv);

	/* Set RX routing */
	if (rx_queues_count > 1)
		tc956xmac_mac_config_rx_queues_routing(priv);

	/* Receive Side Scaling */
	if (rx_queues_count > 1)
		tc956xmac_mac_config_rss(priv);
}

static void tc956xmac_safety_feat_configuration(struct tc956xmac_priv *priv)
{
	if (priv->dma_cap.asp) {
		netdev_info(priv->dev, "Enabling Safety Features\n");
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
		tc956xmac_safety_feat_config(priv, priv->ioaddr, priv->dma_cap.asp);
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */
	} else {
		netdev_info(priv->dev, "No Safety Features support found\n");
	}
}

/**
 * tc956x_rx_crc_pad_config - Rx packets CRC and Padding configuration.
 *
 * @priv: pointer to private structure
 * @crc_pad: CRC padding configuration
 */
static void tc956x_rx_crc_pad_config(struct tc956xmac_priv *priv, u32 crc_pad)
{

#ifdef TC956X
	u32 value = readl(priv->ioaddr + XGMAC_RX_CONFIG);

	if (crc_pad & XGMAC_CONFIG_CST)
		value |= XGMAC_CONFIG_CST;
	else
		value &= ~XGMAC_CONFIG_CST;

	if (crc_pad & XGMAC_CONFIG_ACS)
		value |= XGMAC_CONFIG_ACS;
	else
		value &= ~XGMAC_CONFIG_ACS;

	writel(value, priv->ioaddr + XGMAC_RX_CONFIG);
#endif
}

/**
 * tc956xmac_hw_setup - setup mac in a usable state.
 *  @dev : pointer to the device structure.
 *  Description:
 *  this is the main function to setup the HW in a usable state because the
 *  dma engine is reset, the core registers are configured (e.g. AXI,
 *  Checksum features, timers). The DMA is ready to start receiving and
 *  transmitting.
 *  Return value:
 *  0 on success and an appropriate (-)ve integer as defined in errno.h
 *  file on failure.
 */
static int tc956xmac_hw_setup(struct net_device *dev, bool init_ptp)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
#ifdef TC956X
	u32 rx_cnt = priv->plat->rx_queues_to_use;
	bool enable_en = true;
#endif

	u32 tx_cnt = priv->plat->tx_queues_to_use;
	u32 chan;
	int ret;

	/* DMA initialization and SW reset */
	ret = tc956xmac_init_dma_engine(priv);
	if (ret < 0) {
		netdev_err(priv->dev, "%s: DMA engine initialization failed\n",
			   __func__);
		return ret;
	}

	/* Copy the MAC addr into the HW  */
	tc956xmac_set_umac_addr(priv, priv->hw, dev->dev_addr, HOST_MAC_ADDR_OFFSET, PF_DRIVER);

	/* PS and related bits will be programmed according to the speed */
#ifdef TC956X
	if (priv->hw->pcs || priv->hw->xpcs) {
#else
	if (priv->hw->pcs) {
#endif
		int speed = priv->plat->mac_port_sel_speed;

		if ((speed == SPEED_10) || (speed == SPEED_100) ||
		    (speed == SPEED_1000) || (speed == SPEED_2500) ||
		    (speed == SPEED_10000)) {
			priv->hw->ps = speed;
		} else {
			dev_warn(priv->device, "invalid port speed\n");
			priv->hw->ps = 0;
		}
	}

	/* Initialize the MAC Core */
	tc956xmac_core_init(priv, priv->hw, dev);

	/* Enable Jumbo Frame Support */
	tc956xmac_jumbo_en(priv, dev, TC956X_ENABLE);

	/* Initialize MTL*/
	tc956xmac_mtl_configuration(priv);

	/* Initialize Safety Features */
	tc956xmac_safety_feat_configuration(priv);
	ret = tc956xmac_rx_parser_configuration(priv);

	ret = tc956xmac_rx_ipc(priv, priv->hw);
	if (!ret) {
		netdev_warn(priv->dev, "RX IPC Checksum Offload disabled\n");
		priv->plat->rx_coe = TC956XMAC_RX_COE_NONE;
		priv->hw->rx_csum = 0;
	}

	/* Enable the MAC Rx/Tx */
	tc956xmac_mac_set(priv, priv->ioaddr, true);

	/* Set the HW DMA mode and the COE */
	tc956xmac_dma_operation_mode(priv);

	tc956xmac_mmc_setup(priv);

	/* CRC & Padding Configuration */

	priv->tx_crc_pad_state = TC956X_TX_CRC_PAD_INSERT;
	priv->rx_crc_pad_state = TC956X_RX_CRC_DEFAULT;

	tc956x_rx_crc_pad_config(priv, priv->rx_crc_pad_state);

	if (init_ptp) {
		ret = clk_prepare_enable(priv->plat->clk_ptp_ref);
		if (ret < 0)
			netdev_warn(priv->dev, "failed to enable PTP reference clock: %d\n", ret);

		ret = tc956xmac_init_ptp(priv);
		if (ret == -EOPNOTSUPP)
			netdev_warn(priv->dev, "PTP not supported by HW\n");
		else if (ret)
			netdev_warn(priv->dev, "PTP init failed\n");
	}

	if (priv->use_riwt) {
		if (!priv->rx_riwt)
			priv->rx_riwt = DEF_DMA_RIWT;

		ret = tc956xmac_rx_watchdog(priv, priv->ioaddr, priv->rx_riwt, rx_cnt);
	}

#ifdef TC956X
	if (priv->hw->xpcs) {
		/*C37 AN enable*/
		if (priv->plat->interface == PHY_INTERFACE_MODE_10GKR)
			enable_en = false;
		else if (priv->plat->interface == PHY_INTERFACE_MODE_SGMII) {
			if (priv->is_sgmii_2p5g == true)
				enable_en = false;
			else
				enable_en = true;
		} else
			enable_en = true;
		tc956x_xpcs_ctrl_ane(priv, enable_en);
	}
#else
	if (priv->hw->pcs)
		tc956xmac_pcs_ctrl_ane(priv, priv->ioaddr, 1, priv->hw->ps, 0);
#endif
	tc956x_ptp_configuration(priv, 0);

	/* set TX and RX rings length */
	tc956xmac_set_rings_length(priv);

	/* Enable TSO */
	if (priv->tso) {
		for (chan = 0; chan < tx_cnt; chan++) {
			if (priv->plat->tx_queues_cfg[chan].tso_en)
				tc956xmac_enable_tso(priv, priv->ioaddr, 1, chan);
		}
	}

	/* Enable Split Header */
	if (priv->sph && priv->hw->rx_csum) {
		for (chan = 0; chan < rx_cnt; chan++)
			tc956xmac_enable_sph(priv, priv->ioaddr, 1, chan);
	}

	/* VLAN Tag Insertion */
#ifndef TC956X
	if (priv->dma_cap.vlins)
#else
	if ((priv->dma_cap.vlins) && (dev->features & NETIF_F_HW_VLAN_CTAG_TX))
#endif
		tc956xmac_enable_vlan(priv, priv->hw, TC956XMAC_VLAN_INSERT);

	/* TBS */
	for (chan = 0; chan < tx_cnt; chan++) {
		struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[chan];
		int enable = tx_q->tbs & TC956XMAC_TBS_AVAIL;

		tc956xmac_enable_tbs(priv, priv->ioaddr, enable, chan);
	}

	if (priv->plat->est->enable)
		tc956xmac_est_configure(priv, priv->ioaddr, priv->plat->est,
				   priv->plat->clk_ptp_rate);

	/* Start the ball rolling... */
	tc956xmac_start_all_dma(priv);

	return 0;
}

static void tc956xmac_hw_teardown(struct net_device *dev)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);

	clk_disable_unprepare(priv->plat->clk_ptp_ref);
}

/**
 *  tc956xmac_open - open entry point of the driver
 *  @dev : pointer to the device structure.
 *  Description:
 *  This function is the open entry point of the driver.
 *  Return value:
 *  0 on success and an appropriate (-)ve integer as defined in errno.h
 *  file on failure.
 */
static int tc956xmac_open(struct net_device *dev)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
	int bfsize = 0;
	u32 chan, rd_val;
	int ret;
	struct phy_device *phydev;
	int addr = priv->plat->phy_addr;

	KPRINT_INFO("---> %s : Port %d", __func__, priv->port_num);
	phydev = mdiobus_get_phy(priv->mii, addr);
  
	if (!phydev) {
		netdev_err(priv->dev, "no phy at addr %d\n", addr);
		return -ENODEV;
	}

	if (priv->hw->pcs != TC956XMAC_PCS_RGMII &&
	    priv->hw->pcs != TC956XMAC_PCS_TBI &&
	    priv->hw->pcs != TC956XMAC_PCS_RTBI) {
		ret = tc956xmac_init_phy(dev);
		if (ret) {
			netdev_err(priv->dev,
				   "%s: Cannot attach to PHY (error: %d)\n",
				   __func__, ret);
			KPRINT_INFO("<--- %s(1) : Port %d", __func__, priv->port_num);
			return ret;
		}
	}

	/* Do not re-allocate host resources during resume sequence. Only re-initialize resources */
	if (priv->tc956x_port_pm_suspend == false) {
		/* Extra statistics */
		memset(&priv->xstats, 0, sizeof(struct tc956xmac_extra_stats));
		priv->xstats.threshold = tc;

		bfsize = tc956xmac_set_16kib_bfsize(priv, dev->mtu);
		if (bfsize < 0)
			bfsize = 0;

		if (bfsize < BUF_SIZE_16KiB)
			bfsize = tc956xmac_set_bfsize(dev->mtu, priv->dma_buf_sz);

		priv->dma_buf_sz = bfsize;
		buf_sz = bfsize;

		priv->rx_copybreak = TC956XMAC_RX_COPYBREAK;

		/* Earlier check for TBS */
		for (chan = 0; chan < priv->plat->tx_queues_to_use; chan++) {
			struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[chan];
			int tbs_en = priv->plat->tx_queues_cfg[chan].tbs_en;

			/* Set TC956XMAC_TBS_EN by default. Later allow tc command to
			 *enable/disable
			 */
			tx_q->tbs |= tbs_en ? TC956XMAC_TBS_AVAIL | TC956XMAC_TBS_EN : 0;

			if (tc956xmac_enable_tbs(priv, priv->ioaddr, tbs_en, chan))
				tx_q->tbs &= ~TC956XMAC_TBS_AVAIL;
		}
	}
	ret = alloc_dma_desc_resources(priv);
	if (ret < 0) {
		netdev_err(priv->dev, "%s: DMA descriptors allocation failed\n",
			   __func__);
		goto dma_desc_error;
	}

	ret = init_dma_desc_rings(dev, GFP_KERNEL);
	if (ret < 0) {
		netdev_err(priv->dev, "%s: DMA descriptors initialization failed\n",
			   __func__);
		goto init_error;
	}

	ret = tc956xmac_hw_setup(dev, true);
	if (ret < 0) {
		netdev_err(priv->dev, "%s: Hw setup failed\n", __func__);
		goto init_error;
	}

#ifdef TC956X
	if (priv->port_num == RM_PF0_ID) {
		/* mask all eMAC interrupts for MCU */
		rd_val = readl(priv->ioaddr + INTMCUMASK0);
		rd_val |= 0xFFFF1FFF;
		writel(rd_val, priv->ioaddr + INTMCUMASK0);
	}

	if (priv->port_num == RM_PF1_ID) {
		/* mask all eMAC interrupts for MCU */
		rd_val = readl(priv->ioaddr + INTMCUMASK1);
		rd_val |= 0xFFFF1F80;
		writel(rd_val, priv->ioaddr + INTMCUMASK1);
	}


	/* MSIGEN block is common for Port0 and Port1 */
	rd_val = readl(priv->ioaddr + NCLKCTRL0_OFFSET);
	rd_val |= (1 << 18); /* MSIGENCEN=1 */
#ifdef EEE_MAC_CONTROLLED_MODE
	if (priv->port_num == RM_PF0_ID) {
		rd_val |= (NCLKCTRL0_MAC0312CLKEN | NCLKCTRL0_MAC0125CLKEN);
	}
	rd_val |= (NCLKCTRL0_POEPLLCEN | NCLKCTRL0_SGMPCIEN | NCLKCTRL0_REFCLKOCEN);
#endif
	writel(rd_val, priv->ioaddr + NCLKCTRL0_OFFSET);
	rd_val = readl(priv->ioaddr + NRSTCTRL0_OFFSET);
	rd_val &= ~(1 << 18); /* MSIGENSRST=0 */
#ifdef EEE_MAC_CONTROLLED_MODE
	//rd_val &= ~(NRSTCTRL0_MAC0RST | NRSTCTRL0_MAC0RST);
#endif
	writel(rd_val, priv->ioaddr + NRSTCTRL0_OFFSET);


	/* Initialize MSIGEN */

	/* MSI_OUT_EN: Disable all first */
	writel(0x00000000, priv->ioaddr + TC956X_MSI_OUT_EN_OFFSET(priv->port_num));
	/* MSI_MASK_SET: mask all vectors other than vector 0 */
	writel(0xfffffffe, priv->ioaddr + TC956X_MSI_MASK_SET_OFFSET(priv->port_num));
	/* MSI_MASK_CLR: unmask vector 0 */
	writel(0x00000001, priv->ioaddr + TC956X_MSI_MASK_CLR_OFFSET(priv->port_num));
	/* MSI_VECT_SET0: All INTs mapped to vector 0 */
	writel(0x00000000, priv->ioaddr + TC956X_MSI_VECT_SET0_OFFSET(priv->port_num));
	/* MSI_VECT_SET1: All INTs mapped to vector 0 */
	writel(0x00000000, priv->ioaddr + TC956X_MSI_VECT_SET1_OFFSET(priv->port_num));
	/* MSI_VECT_SET2: All INTs mapped to vector 0 */
	writel(0x00000000, priv->ioaddr + TC956X_MSI_VECT_SET2_OFFSET(priv->port_num));
	/* MSI_VECT_SET3: All INTs mapped to vector 0 */
	writel(0x00000000, priv->ioaddr + TC956X_MSI_VECT_SET3_OFFSET(priv->port_num));
	/* MSI_VECT_SET4: All INTs mapped to vector 0 */
	writel(0x00000000, priv->ioaddr + TC956X_MSI_VECT_SET4_OFFSET(priv->port_num));
	/* MSI_VECT_SET5: All INTs mapped to vector 0 */
	writel(0x00000000, priv->ioaddr + TC956X_MSI_VECT_SET5_OFFSET(priv->port_num));
	/* MSI_VECT_SET6: All INTs mapped to vector 0 */
	writel(0x00000000, priv->ioaddr + TC956X_MSI_VECT_SET6_OFFSET(priv->port_num));
	/* MSI_VECT_SET7: All INTs mapped to vector 0 */
	writel(0x00000000, priv->ioaddr + TC956X_MSI_VECT_SET7_OFFSET(priv->port_num));

	/* Disable MSI for Tx/Rx channels that do not belong to Host */
	rd_val = 0;
	for (chan = 0; chan < MTL_MAX_TX_QUEUES; chan++) {
		if (priv->plat->tx_dma_ch_owner[chan] != USE_IN_TC956X_SW)
			rd_val |= (1 << (MSI_INT_TX_CH0 + chan));
	}

	for (chan = 0; chan < MTL_MAX_RX_QUEUES; chan++) {
		if (priv->plat->rx_dma_ch_owner[chan] != USE_IN_TC956X_SW)
			rd_val |= (1 << (MSI_INT_RX_CH0 + chan));
	}

	if (phydev->interrupts ==  PHY_INTERRUPT_DISABLED) {
		/* PHY MSI interrupt diabled */
		rd_val |= (1 << MSI_INT_EXT_PHY);
	}

	/* rd_val |= (1 << 2); *//* Disable MSI for MAC EVENT Interrupt */
	/* Disable MAC Event and XPCS interrupt */
	rd_val = ENABLE_MSI_INTR & (~rd_val);

#ifdef TC956X_SW_MSI
	/* Enable SW MSI interrupt */
	KPRINT_INFO("%s Enable SW MSI", __func__);
	rd_val |=  (1 << MSI_INT_SW_MSI);

	/*Clear SW MSI*/
	writel(1, priv->ioaddr + TC956X_MSI_SW_MSI_CLR(priv->port_num));

#endif
	writel(rd_val, priv->ioaddr + TC956X_MSI_OUT_EN_OFFSET(priv->port_num)); /* MSI_OUT_EN: Enable All mac int */

#endif

	tc956xmac_init_coalesce(priv);

	if (priv->phylink)
		phylink_start(priv->phylink);

	KPRINT_INFO("%s phylink started", __func__);

	/* Request the IRQ lines */
	ret = request_irq(dev->irq, tc956xmac_interrupt,
			  IRQF_NO_SUSPEND, dev->name, dev);
	if (unlikely(ret < 0)) {
		netdev_err(priv->dev,
			   "%s: ERROR: allocating the IRQ %d (error: %d)\n",
			   __func__, dev->irq, ret);
		goto irq_error;
	}

	/* Do not re-request WOL irq resources during resume sequence. */
	if (priv->tc956x_port_pm_suspend == false) {
		/* Request the Wake IRQ in case of another line is used for WoL */
		if (priv->wol_irq != dev->irq) {
			ret = request_irq(priv->wol_irq, tc956xmac_wol_interrupt,
					  IRQF_NO_SUSPEND, dev->name, dev);
			if (unlikely(ret < 0)) {
				netdev_err(priv->dev,
					   "%s: ERROR: allocating the WoL IRQ %d (%d)\n",
					   __func__, priv->wol_irq, ret);
				goto wolirq_error;
			}
		}
#ifndef TC956X
		/* Request the IRQ lines */
		if (priv->lpi_irq > 0) {
			ret = request_irq(priv->lpi_irq, tc956xmac_interrupt, IRQF_SHARED,
					  dev->name, dev);
			if (unlikely(ret < 0)) {
				netdev_err(priv->dev,
					   "%s: ERROR: allocating the LPI IRQ %d (%d)\n",
					   __func__, priv->lpi_irq, ret);
				goto lpiirq_error;
			}
		}
#endif
		priv->tc956xmac_pm_wol_interrupt = false; /* Initialize flag for PHY Work queue */
	}
	tc956xmac_enable_all_queues(priv);
	tc956xmac_start_all_queues(priv);

#ifdef TC956X
	if (readl_poll_timeout_atomic(priv->ioaddr +  TC956X_MSI_EVENT_OFFSET(priv->port_num),
					rd_val, !(rd_val & 0x1), 100, 10000)) {

		netdev_warn(priv->dev, "MSI Vector not clear. MSI_MASK_CLR = 0x0%x\n",
				readl(priv->ioaddr +  TC956X_MSI_MASK_CLR_OFFSET(priv->port_num)));

	}

	/* MSI_MASK_CLR: unmask vector 0 */
	writel(0x00000001, priv->ioaddr + TC956X_MSI_MASK_CLR_OFFSET(priv->port_num));

#ifdef TX_COMPLETION_WITHOUT_TIMERS
		writel(0, priv->tc956x_SRAM_pci_base_addr
				+ TX_TIMER_SRAM_OFFSET(priv->port_num));
#endif

#endif
	KPRINT_INFO("<--- %s(2) : Port %d", __func__, priv->port_num);
	return 0;
#ifndef TC956X
lpiirq_error:
	if (priv->wol_irq != dev->irq)
		free_irq(priv->wol_irq, dev);
#endif
wolirq_error:
	free_irq(dev->irq, dev);
irq_error:
	phylink_stop(priv->phylink);
#ifdef ENABLE_TX_TIMER
	for (chan = 0; chan < priv->plat->tx_queues_to_use; chan++) {
		if (priv->plat->tx_dma_ch_owner[chan] == USE_IN_TC956X_SW)
			del_timer_sync(&priv->tx_queue[chan].txtimer);
	}
#endif
	tc956xmac_hw_teardown(dev);
init_error:
	free_dma_desc_resources(priv);
dma_desc_error:
	phylink_disconnect_phy(priv->phylink);
	KPRINT_INFO("<--- %s(3) : Port %d", __func__, priv->port_num);
	return ret;
}

/**
 *  tc956xmac_release - close entry point of the driver
 *  @dev : device pointer.
 *  Description:
 *  This is the stop entry point of the driver.
 */
static int tc956xmac_release(struct net_device *dev)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
#ifdef ENABLE_TX_TIMER
	u32 chan;
#endif
	KPRINT_INFO("---> %s : Port %d", __func__, priv->port_num);
#ifdef TX_COMPLETION_WITHOUT_TIMERS
		writel(0, priv->tc956x_SRAM_pci_base_addr
				+ TX_TIMER_SRAM_OFFSET(priv->port_num));

#endif
	/* Stop and disconnect the PHY */
	if (priv->phylink) {
		phylink_stop(priv->phylink);
		phylink_disconnect_phy(priv->phylink);
	}
	tc956xmac_stop_all_queues(priv);

	tc956xmac_disable_all_queues(priv);
#ifdef ENABLE_TX_TIMER
	for (chan = 0; chan < priv->plat->tx_queues_to_use; chan++) {
		if (priv->plat->tx_dma_ch_owner[chan] == USE_IN_TC956X_SW)
			del_timer_sync(&priv->tx_queue[chan].txtimer);
	}
#endif
	/* Free the IRQ lines */
	free_irq(dev->irq, dev);
	/* Do not Free Host Irq resources during suspend sequence */
	if (priv->tc956x_port_pm_suspend == false) {
		if (priv->wol_irq != dev->irq)
			free_irq(priv->wol_irq, dev);
#ifndef TC956X
		if (priv->lpi_irq > 0)
			free_irq(priv->lpi_irq, dev);
#endif
	}
	/* Stop TX/RX DMA and clear the descriptors */
	tc956xmac_stop_all_dma(priv);

	/* Release and free the Rx/Tx resources */
	free_dma_desc_resources(priv);

	/* Disable the MAC Rx/Tx */
	tc956xmac_mac_set(priv, priv->ioaddr, false);

	netif_carrier_off(dev);

	tc956xmac_release_ptp(priv);

	KPRINT_INFO("<--- %s : Port %d", __func__, priv->port_num);
	return 0;
}

static bool tc956xmac_vlan_insert(struct tc956xmac_priv *priv, struct sk_buff *skb,
			       struct tc956xmac_tx_queue *tx_q)
{
	u16 tag = 0x0, inner_tag = 0x0;
	u32 inner_type = 0x0;
	struct dma_desc *p;

	if (!priv->dma_cap.vlins)
		return false;
	if (!skb_vlan_tag_present(skb))
		return false;
	if (skb->vlan_proto == htons(ETH_P_8021AD)) {
		inner_tag = skb_vlan_tag_get(skb);
		inner_type = TC956XMAC_VLAN_INSERT;
	}

	tag = skb_vlan_tag_get(skb);

	if (tx_q->tbs & TC956XMAC_TBS_AVAIL)
		p = &tx_q->dma_entx[tx_q->cur_tx].basic;
	else
		p = &tx_q->dma_tx[tx_q->cur_tx];

	if (tc956xmac_set_desc_vlan_tag(priv, p, tag, inner_tag, inner_type))
		return false;

	tc956xmac_set_tx_owner(priv, p);
	tx_q->cur_tx = TC956XMAC_GET_ENTRY(tx_q->cur_tx, DMA_TX_SIZE);
	return true;
}

/**
 *  tc956xmac_tso_allocator - close entry point of the driver
 *  @priv: driver private structure
 *  @des: buffer start address
 *  @total_len: total length to fill in descriptors
 *  @last_segmant: condition for the last descriptor
 *  @queue: TX queue index
 *  Description:
 *  This function fills descriptor and request new descriptors according to
 *  buffer length to fill
 */
static void tc956xmac_tso_allocator(struct tc956xmac_priv *priv, dma_addr_t des,
				 int total_len, bool last_segment, u32 queue)
{
	struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[queue];
	struct dma_desc *desc;
	u32 buff_size;
	int tmp_len;

	tmp_len = total_len;

	while (tmp_len > 0) {
		dma_addr_t curr_addr;

		tx_q->cur_tx = TC956XMAC_GET_ENTRY(tx_q->cur_tx, DMA_TX_SIZE);
		WARN_ON(tx_q->tx_skbuff[tx_q->cur_tx]);

		if (tx_q->tbs & TC956XMAC_TBS_AVAIL)
			desc = &tx_q->dma_entx[tx_q->cur_tx].basic;
		else
			desc = &tx_q->dma_tx[tx_q->cur_tx];

		curr_addr = des + (total_len - tmp_len);
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
		if (priv->dma_cap.addr64 <= 32)
			desc->des0 = cpu_to_le32(curr_addr);
		else
#endif
			tc956xmac_set_desc_addr(priv, desc, curr_addr);

		buff_size = tmp_len >= TSO_MAX_BUFF_SIZE ?
			    TSO_MAX_BUFF_SIZE : tmp_len;

		tc956xmac_prepare_tso_tx_desc(priv, desc, 0, buff_size,
				0, 1,
				(last_segment) && (tmp_len <= TSO_MAX_BUFF_SIZE),
				0, 0);

		tmp_len -= TSO_MAX_BUFF_SIZE;
	}
}

/**
 *  tc956xmac_tso_xmit - Tx entry point of the driver for oversized frames (TSO)
 *  @skb : the socket buffer
 *  @dev : device pointer
 *  Description: this is the transmit function that is called on TSO frames
 *  (support available on GMAC4 and newer chips).
 *  Diagram below show the ring programming in case of TSO frames:
 *
 *  First Descriptor
 *   --------
 *   | DES0 |---> buffer1 = L2/L3/L4 header
 *   | DES1 |---> TCP Payload (can continue on next descr...)
 *   | DES2 |---> buffer 1 and 2 len
 *   | DES3 |---> must set TSE, TCP hdr len-> [22:19]. TCP payload len [17:0]
 *   --------
 *	|
 *     ...
 *	|
 *   --------
 *   | DES0 | --| Split TCP Payload on Buffers 1 and 2
 *   | DES1 | --|
 *   | DES2 | --> buffer 1 and 2 len
 *   | DES3 |
 *   --------
 *
 * mss is fixed when enable tso, so w/o programming the TDES3 ctx field.
 */
static netdev_tx_t tc956xmac_tso_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct dma_desc *desc, *first, *mss_desc = NULL;
	struct tc956xmac_priv *priv = netdev_priv(dev);
	int desc_size, tmp_pay_len = 0, first_tx;
	int nfrags = skb_shinfo(skb)->nr_frags;
	u32 queue = skb_get_queue_mapping(skb);
	unsigned int first_entry, tx_packets;
	struct tc956xmac_tx_queue *tx_q;
	bool has_vlan, set_ic;
	u8 proto_hdr_len, hdr;
	u32 pay_len, mss;
	dma_addr_t des;
	int i;

	tx_q = &priv->tx_queue[queue];
	first_tx = tx_q->cur_tx;

	/* Compute header lengths */
	if (skb_shinfo(skb)->gso_type & SKB_GSO_UDP_L4) {
		proto_hdr_len = skb_transport_offset(skb) + sizeof(struct udphdr);
		hdr = sizeof(struct udphdr);
	} else {
		proto_hdr_len = skb_transport_offset(skb) + tcp_hdrlen(skb);
		hdr = tcp_hdrlen(skb);
	}

	/* Desc availability based on threshold should be enough safe */
	if (unlikely(tc956xmac_tx_avail(priv, queue) <
		(((skb->len - proto_hdr_len) / TSO_MAX_BUFF_SIZE + 1)))) {
		if (!netif_tx_queue_stopped(netdev_get_tx_queue(dev, queue))) {
			netif_tx_stop_queue(netdev_get_tx_queue(priv->dev,
								queue));
			/* This is a hard error, log it. */
			netdev_err(priv->dev,
				   "%s: Tx Ring full when queue awake\n",
				   __func__);
		}
		return NETDEV_TX_BUSY;
	}

	pay_len = skb_headlen(skb) - proto_hdr_len; /* no frags */

	mss = skb_shinfo(skb)->gso_size;

	/* set new MSS value if needed */
	if (mss != tx_q->mss) {
		if (tx_q->tbs & TC956XMAC_TBS_AVAIL)
			mss_desc = &tx_q->dma_entx[tx_q->cur_tx].basic;
		else
			mss_desc = &tx_q->dma_tx[tx_q->cur_tx];

		tc956xmac_set_mss(priv, mss_desc, mss);
		tx_q->mss = mss;
		tx_q->cur_tx = TC956XMAC_GET_ENTRY(tx_q->cur_tx, DMA_TX_SIZE);
		WARN_ON(tx_q->tx_skbuff[tx_q->cur_tx]);
	}

	if (netif_msg_tx_queued(priv)) {
		pr_info("%s: hdrlen %d, hdr_len %d, pay_len %d, mss %d\n",
			__func__, hdr, proto_hdr_len, pay_len, mss);
		pr_info("\tskb->len %d, skb->data_len %d\n", skb->len,
			skb->data_len);
	}

	/* Check if VLAN can be inserted by HW */
	has_vlan = tc956xmac_vlan_insert(priv, skb, tx_q);

	first_entry = tx_q->cur_tx;
	WARN_ON(tx_q->tx_skbuff[first_entry]);

	if (tx_q->tbs & TC956XMAC_TBS_AVAIL)
		desc = &tx_q->dma_entx[first_entry].basic;
	else
		desc = &tx_q->dma_tx[first_entry];
	first = desc;

	if (has_vlan)
		tc956xmac_set_desc_vlan(priv, first, TC956XMAC_VLAN_INSERT);

	/* first descriptor: fill Headers on Buf1 */
	des = dma_map_single(priv->device, skb->data, skb_headlen(skb),
			     DMA_TO_DEVICE);
	if (dma_mapping_error(priv->device, des))
		goto dma_map_err;

	tx_q->tx_skbuff_dma[first_entry].buf = des;
	tx_q->tx_skbuff_dma[first_entry].len = skb_headlen(skb);

#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	if (priv->dma_cap.addr64 <= 32) {
		first->des0 = cpu_to_le32(des);

		/* Fill start of payload in buff2 of first descriptor */
		if (pay_len)
			first->des1 = cpu_to_le32(des + proto_hdr_len);

		/* If needed take extra descriptors to fill the remaining
		 * payload
		 */
		tmp_pay_len = pay_len - TSO_MAX_BUFF_SIZE;
	} else {
#endif
		tc956xmac_set_desc_addr(priv, first, des);
		tmp_pay_len = pay_len;
		des += proto_hdr_len;
		pay_len = 0;
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	}
#endif

	tc956xmac_tso_allocator(priv, des, tmp_pay_len, (nfrags == 0), queue);

	/* Prepare fragments */
	for (i = 0; i < nfrags; i++) {
		const skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		des = skb_frag_dma_map(priv->device, frag, 0,
				       skb_frag_size(frag),
				       DMA_TO_DEVICE);
		if (dma_mapping_error(priv->device, des))
			goto dma_map_err;

		tc956xmac_tso_allocator(priv, des, skb_frag_size(frag),
				     (i == nfrags - 1), queue);

		tx_q->tx_skbuff_dma[tx_q->cur_tx].buf = des;
		tx_q->tx_skbuff_dma[tx_q->cur_tx].len = skb_frag_size(frag);
		tx_q->tx_skbuff_dma[tx_q->cur_tx].map_as_page = true;
	}

	tx_q->tx_skbuff_dma[tx_q->cur_tx].last_segment = true;

	/* Only the last descriptor gets to point to the skb. */
	tx_q->tx_skbuff[tx_q->cur_tx] = skb;

	/* Manage tx mitigation */
	tx_packets = (tx_q->cur_tx + 1) - first_tx;
	tx_q->tx_count_frames += tx_packets;

	if ((skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) && priv->hwts_tx_en)
		set_ic = true;
	else if (!priv->tx_coal_frames)
		set_ic = false;
	else if (tx_packets > priv->tx_coal_frames)
		set_ic = true;
	else if ((tx_q->tx_count_frames % priv->tx_coal_frames) < tx_packets)
		set_ic = true;
	else
		set_ic = false;

	if (set_ic) {
		if (tx_q->tbs & TC956XMAC_TBS_AVAIL)
			desc = &tx_q->dma_entx[tx_q->cur_tx].basic;
		else
			desc = &tx_q->dma_tx[tx_q->cur_tx];

		tx_q->tx_count_frames = 0;
		tc956xmac_set_tx_ic(priv, desc);
		priv->xstats.tx_set_ic_bit++;
	}

	/* We've used all descriptors we need for this skb, however,
	 * advance cur_tx so that it references a fresh descriptor.
	 * ndo_start_xmit will fill this descriptor the next time it's
	 * called and tc956xmac_tx_clean may clean up to this descriptor.
	 */
	tx_q->cur_tx = TC956XMAC_GET_ENTRY(tx_q->cur_tx, DMA_TX_SIZE);

	if (unlikely(tc956xmac_tx_avail(priv, queue) <= (MAX_SKB_FRAGS + 1))) {
		netif_dbg(priv, hw, priv->dev, "%s: stop transmitted packets\n",
			  __func__);
		netif_tx_stop_queue(netdev_get_tx_queue(priv->dev, queue));
	}

	dev->stats.tx_bytes += skb->len;
	priv->xstats.tx_tso_frames[queue]++;
	priv->xstats.tx_tso_nfrags[queue] += nfrags;

	if (priv->sarc_type)
		tc956xmac_set_desc_sarc(priv, first, priv->sarc_type);

	skb_tx_timestamp(skb);

	if (unlikely((skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) &&
		     priv->hwts_tx_en)) {
		/* declare that device is doing timestamping */
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
		tc956xmac_enable_tx_timestamp(priv, first);
	}

	/* Complete the first descriptor before granting the DMA */
	tc956xmac_prepare_tso_tx_desc(priv, first, 1,
			proto_hdr_len,
			pay_len,
			1, tx_q->tx_skbuff_dma[first_entry].last_segment,
			hdr / 4, (skb->len - proto_hdr_len));

	/* If context desc is used to change MSS */
	if (mss_desc) {
		/* Make sure that first descriptor has been completely
		 * written, including its own bit. This is because MSS is
		 * actually before first descriptor, so we need to make
		 * sure that MSS's own bit is the last thing written.
		 */
		dma_wmb();
		tc956xmac_set_tx_owner(priv, mss_desc);
	}

	/* The own bit must be the latest setting done when prepare the
	 * descriptor and then barrier is needed to make sure that
	 * all is coherent before granting the DMA engine.
	 */
	wmb();

	if (netif_msg_pktdata(priv)) {
		pr_info("%s: curr=%d dirty=%d f=%d, e=%d, f_p=%p, nfrags %d\n",
			__func__, tx_q->cur_tx, tx_q->dirty_tx, first_entry,
			tx_q->cur_tx, first, nfrags);
		pr_info(">>> frame to be transmitted: ");
		print_pkt(skb->data, skb_headlen(skb));
	}

	netdev_tx_sent_queue(netdev_get_tx_queue(dev, queue), skb->len);

	if (tx_q->tbs & TC956XMAC_TBS_AVAIL)
		desc_size = sizeof(struct dma_edesc);
	else
		desc_size = sizeof(struct dma_desc);

	tx_q->tx_tail_addr = tx_q->dma_tx_phy + (tx_q->cur_tx * desc_size);
	tc956xmac_set_tx_tail_ptr(priv, priv->ioaddr, tx_q->tx_tail_addr, queue);
#ifdef ENABLE_TX_TIMER
	tc956xmac_tx_timer_arm(priv, queue);
#endif

#ifdef TX_COMPLETION_WITHOUT_TIMERS
	writel(1, priv->tc956x_SRAM_pci_base_addr
			+ TX_TIMER_SRAM_OFFSET(priv->port_num));
#endif
	return NETDEV_TX_OK;

dma_map_err:
	dev_err(priv->device, "Tx dma map failed\n");
	dev_kfree_skb(skb);
	priv->dev->stats.tx_dropped++;
	return NETDEV_TX_OK;
}

/**
 *  tc956xmac_xmit - Tx entry point of the driver
 *  @skb : the socket buffer
 *  @dev : device pointer
 *  Description : this is the tx entry point of the driver.
 *  It programs the chain or the ring and supports oversized frames
 *  and SG feature.
 */
static netdev_tx_t tc956xmac_xmit(struct sk_buff *skb, struct net_device *dev)
{
	unsigned int first_entry, tx_packets, enh_desc;
	struct tc956xmac_priv *priv = netdev_priv(dev);
	unsigned int nopaged_len = skb_headlen(skb);
	int i, csum_insertion = 0, is_jumbo = 0;
	u32 queue = skb_get_queue_mapping(skb);
	int nfrags = skb_shinfo(skb)->nr_frags;
	int gso = skb_shinfo(skb)->gso_type;
	struct dma_edesc *tbs_desc = NULL;
	int entry, desc_size, first_tx;
	struct dma_desc *desc, *first;
	struct tc956xmac_tx_queue *tx_q;
	bool has_vlan, set_ic;
	dma_addr_t des;
	u64 ns = 0;
	u32 ts_low, ts_high;

	tx_q = &priv->tx_queue[queue];
	first_tx = tx_q->cur_tx;

	KPRINT_DEBUG1("tso en = %d\n", priv->tso);
	KPRINT_DEBUG1("skb tso en = %d\n", skb_is_gso(skb));
	/* Manage oversized TCP frames for GMAC4 device */
	if (skb_is_gso(skb) && priv->tso) {
		KPRINT_DEBUG1("XMIT TSO IF\n");
		if (gso & (SKB_GSO_TCPV4 | SKB_GSO_TCPV6))
			return tc956xmac_tso_xmit(skb, dev);
		if (priv->plat->has_gmac4 && (gso & SKB_GSO_UDP_L4))
			return tc956xmac_tso_xmit(skb, dev);
	}
	KPRINT_DEBUG1("XMIT Normal\n");

	if (unlikely(tc956xmac_tx_avail(priv, queue) < nfrags + 1)) {
		if (!netif_tx_queue_stopped(netdev_get_tx_queue(dev, queue))) {
			netif_tx_stop_queue(netdev_get_tx_queue(priv->dev,
								queue));
			/* This is a hard error, log it. */
			netdev_err(priv->dev,
				   "%s: Tx Ring full when queue awake\n",
				   __func__);
		}
		return NETDEV_TX_BUSY;
	}

	/* Prepare context descriptor for one-step timestamp correction */
	if ((tx_q->queue_index == TC956X_GPTP_TX_CH) && (priv->ost_en == 1)) {
		if (tx_q->tbs & TC956XMAC_TBS_AVAIL)
			desc = &tx_q->dma_entx[tx_q->cur_tx].basic;
		else
			desc = &tx_q->dma_tx[tx_q->cur_tx];
		tc956xmac_get_mac_tx_timestamp(priv, priv->hw, &ns);
		ts_low = (u32)ns;
		ts_high = (u32)(ns >> 32);
		tc956xmac_set_desc_ostc(priv, desc, ts_high, ts_low);
		tx_q->cur_tx = TC956XMAC_GET_ENTRY(tx_q->cur_tx, DMA_TX_SIZE);
		WARN_ON(tx_q->tx_skbuff[tx_q->cur_tx]);
	}

	/* Check if VLAN can be inserted by HW */
	has_vlan = tc956xmac_vlan_insert(priv, skb, tx_q);

	entry = tx_q->cur_tx;
	first_entry = entry;
	WARN_ON(tx_q->tx_skbuff[first_entry]);

	/* Update checksum value as per Ethtool configuration */

	/*csum_insertion = (skb->ip_summed == CHECKSUM_PARTIAL);*/
	csum_insertion = priv->csum_insertion;
	KPRINT_DEBUG1("csum_insertion = %d\n", csum_insertion);
	KPRINT_DEBUG1("priv->tx_crc_pad_state = %d\n", priv->tx_crc_pad_state);

	if (likely(priv->extend_desc))
		desc = (struct dma_desc *)(tx_q->dma_etx + entry);
	else if (tx_q->tbs & TC956XMAC_TBS_AVAIL)
		desc = &tx_q->dma_entx[entry].basic;
	else
		desc = tx_q->dma_tx + entry;

	first = desc;

	if (has_vlan)
		tc956xmac_set_desc_vlan(priv, first, TC956XMAC_VLAN_INSERT);

	enh_desc = priv->plat->enh_desc;
	/* To program the descriptors according to the size of the frame */
	if (enh_desc)
		is_jumbo = tc956xmac_is_jumbo_frm(priv, skb->len, enh_desc);

	if (unlikely(is_jumbo)) {
		entry = tc956xmac_jumbo_frm(priv, tx_q, skb, csum_insertion);
		if (unlikely(entry < 0) && (entry != -EINVAL))
			goto dma_map_err;
	}

	for (i = 0; i < nfrags; i++) {
		const skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
		int len = skb_frag_size(frag);
		bool last_segment = (i == (nfrags - 1));

		entry = TC956XMAC_GET_ENTRY(entry, DMA_TX_SIZE);
		WARN_ON(tx_q->tx_skbuff[entry]);

		if (likely(priv->extend_desc))
			desc = (struct dma_desc *)(tx_q->dma_etx + entry);
		else if (tx_q->tbs & TC956XMAC_TBS_AVAIL)
			desc = &tx_q->dma_entx[entry].basic;
		else
			desc = tx_q->dma_tx + entry;

		des = skb_frag_dma_map(priv->device, frag, 0, len,
				       DMA_TO_DEVICE);
		if (dma_mapping_error(priv->device, des))
			goto dma_map_err; /* should reuse desc w/o issues */

		tx_q->tx_skbuff_dma[entry].buf = des;

		tc956xmac_set_desc_addr(priv, desc, des);

		tx_q->tx_skbuff_dma[entry].map_as_page = true;
		tx_q->tx_skbuff_dma[entry].len = len;
		tx_q->tx_skbuff_dma[entry].last_segment = last_segment;

		/* Prepare the descriptor and set the own bit too */
		tc956xmac_prepare_tx_desc(priv, desc, 0, len, csum_insertion,
				priv->tx_crc_pad_state,	priv->mode, 1,
				last_segment, skb->len);
	}

	/* Only the last descriptor gets to point to the skb. */
	tx_q->tx_skbuff[entry] = skb;

	/* According to the coalesce parameter the IC bit for the latest
	 * segment is reset and the timer re-started to clean the tx status.
	 * This approach takes care about the fragments: desc is the first
	 * element in case of no SG.
	 */
	tx_packets = (entry + 1) - first_tx;
	tx_q->tx_count_frames += tx_packets;

	if ((skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) && priv->hwts_tx_en)
		set_ic = true;
	else if (!priv->tx_coal_frames)
		set_ic = false;
	else if (tx_packets > priv->tx_coal_frames)
		set_ic = true;
	else if ((tx_q->tx_count_frames % priv->tx_coal_frames) < tx_packets)
		set_ic = true;
	else
		set_ic = false;

	if (set_ic) {
		if (likely(priv->extend_desc))
			desc = &tx_q->dma_etx[entry].basic;
		else if (tx_q->tbs & TC956XMAC_TBS_AVAIL)
			desc = &tx_q->dma_entx[entry].basic;
		else
			desc = &tx_q->dma_tx[entry];

		tx_q->tx_count_frames = 0;
		tc956xmac_set_tx_ic(priv, desc);
		priv->xstats.tx_set_ic_bit++;
	}

	/* We've used all descriptors we need for this skb, however,
	 * advance cur_tx so that it references a fresh descriptor.
	 * ndo_start_xmit will fill this descriptor the next time it's
	 * called and tc956xmac_tx_clean may clean up to this descriptor.
	 */
	entry = TC956XMAC_GET_ENTRY(entry, DMA_TX_SIZE);
	tx_q->cur_tx = entry;

	if (netif_msg_pktdata(priv)) {
		netdev_dbg(priv->dev,
			   "%s: curr=%d dirty=%d f=%d, e=%d, first=%p, nfrags=%d",
			   __func__, tx_q->cur_tx, tx_q->dirty_tx, first_entry,
			   entry, first, nfrags);

		netdev_dbg(priv->dev, ">>> frame to be transmitted: ");
		print_pkt(skb->data, skb->len);
	}

	if (unlikely(tc956xmac_tx_avail(priv, queue) <= (MAX_SKB_FRAGS + 1))) {
		netif_dbg(priv, hw, priv->dev, "%s: stop transmitted packets\n",
			  __func__);
		netif_tx_stop_queue(netdev_get_tx_queue(priv->dev, queue));
	}

	dev->stats.tx_bytes += skb->len;

	if (priv->sarc_type)
		tc956xmac_set_desc_sarc(priv, first, priv->sarc_type);

	skb_tx_timestamp(skb);

	/* Ready to fill the first descriptor and set the OWN bit w/o any
	 * problems because all the descriptors are actually ready to be
	 * passed to the DMA engine.
	 */
	if (likely(!is_jumbo)) {
		bool last_segment = (nfrags == 0);

		des = dma_map_single(priv->device, skb->data,
				     nopaged_len, DMA_TO_DEVICE);
		if (dma_mapping_error(priv->device, des))
			goto dma_map_err;

		tx_q->tx_skbuff_dma[first_entry].buf = des;

		tc956xmac_set_desc_addr(priv, first, des);

		tx_q->tx_skbuff_dma[first_entry].len = nopaged_len;
		tx_q->tx_skbuff_dma[first_entry].last_segment = last_segment;

		if (unlikely((skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) &&
			     priv->hwts_tx_en)) {
			/* declare that device is doing timestamping */
			skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
			tc956xmac_enable_tx_timestamp(priv, first);
		}

		/* Prepare the first descriptor setting the OWN bit too */
		tc956xmac_prepare_tx_desc(priv, first, 1, nopaged_len,
				csum_insertion, priv->tx_crc_pad_state,
				priv->mode, 0, last_segment, skb->len);
	}

	if (tx_q->tbs & TC956XMAC_TBS_EN) {
		/* Check if timestamp_valid bit is enabled */
		if (skb->data[19] & 0x01) {
			struct timespec64 ts = ns_to_timespec64(skb->tstamp);
			u32 Presentation_time, Traverse_time, app_launch_time;
			u64 ns, lt;
#ifndef CONFIG_ARCH_DMA_ADDR_T_64BIT
			u64 quotient;
			u32 reminder;
#endif
			unsigned long flags;

			if (skb->tstamp) {
				ts = ns_to_timespec64(skb->tstamp);
			} else {
				Presentation_time = skb->data[30];
				Presentation_time = (Presentation_time<<8) |
							 skb->data[31];
				Presentation_time = (Presentation_time<<8) |
							skb->data[32];
				Presentation_time = (Presentation_time<<8) |
							skb->data[33];

				if (AVB_CLASS_B_TX_CH == queue)
					Traverse_time = 50000000; /* Class B - 50ms */
				else if (AVB_CLASS_A_TX_CH == queue)
					Traverse_time = 2000000; /* Class A- 2ms */
				else
					Traverse_time = 0; /* default */

				if (Presentation_time >= Traverse_time)
					app_launch_time =
						Presentation_time - Traverse_time;
				else
					app_launch_time =
						0x100000000ULL - Traverse_time +
						 Presentation_time;

				spin_lock_irqsave(&priv->ptp_lock, flags);
				tc956xmac_get_systime(priv, priv->ptpaddr, &ns);
				spin_unlock_irqrestore(&priv->ptp_lock, flags);

				lt = ((ns >> 32) << 32) | app_launch_time;
				if (((signed long)app_launch_time - (signed long)(ns & 0xFFFFFFFF)) < 0) {
					/* If the difference is 2 seconds apart, it is judged as rollover */
					if (((ns & 0xFFFFFFFF) - app_launch_time) > ((1ULL << 32) / 2))
						lt += (u64)((u64)1 << 32);
				}
#ifndef CONFIG_ARCH_DMA_ADDR_T_64BIT
				quotient = div_u64_rem(lt, 1000000000ULL,
						&reminder);
				ts.tv_sec = quotient & 0xFF;
				ts.tv_nsec = reminder;
#else
				ts.tv_sec  = (lt / 1000000000) & 0xFF;
				ts.tv_nsec = (lt % 1000000000);
#endif
			}
			tbs_desc = &tx_q->dma_entx[first_entry];
			tc956xmac_set_desc_tbs(priv, tbs_desc, ts.tv_sec, ts.tv_nsec, true);
		} else {
			tbs_desc = &tx_q->dma_entx[first_entry];
			tc956xmac_set_desc_tbs(priv, tbs_desc, 0, 0, false);
		}
	}

	tc956xmac_set_tx_owner(priv, first);

	/* The own bit must be the latest setting done when prepare the
	 * descriptor and then barrier is needed to make sure that
	 * all is coherent before granting the DMA engine.
	 */
	wmb();

	netdev_tx_sent_queue(netdev_get_tx_queue(dev, queue), skb->len);

	tc956xmac_enable_dma_transmission(priv, priv->ioaddr);

	if (likely(priv->extend_desc))
		desc_size = sizeof(struct dma_extended_desc);
	else if (tx_q->tbs & TC956XMAC_TBS_AVAIL)
		desc_size = sizeof(struct dma_edesc);
	else
		desc_size = sizeof(struct dma_desc);

	tx_q->tx_tail_addr = tx_q->dma_tx_phy + (tx_q->cur_tx * desc_size);
	tc956xmac_set_tx_tail_ptr(priv, priv->ioaddr, tx_q->tx_tail_addr, queue);

#ifdef ENABLE_TX_TIMER
	tc956xmac_tx_timer_arm(priv, queue);
#endif

#ifdef TX_COMPLETION_WITHOUT_TIMERS
	writel(1, priv->tc956x_SRAM_pci_base_addr
			+ TX_TIMER_SRAM_OFFSET(priv->port_num));
#endif

	return NETDEV_TX_OK;

dma_map_err:
	netdev_err(priv->dev, "Tx DMA map failed\n");
	dev_kfree_skb(skb);
	priv->dev->stats.tx_dropped++;
	return NETDEV_TX_OK;
}

#ifndef TC956X
/**
 *  tc956xmac_rx_vlan - Rx VLAN Stripping Function
 *  @dev : device pointer
 *  @skb : the socket buffer
 *  Description : this function strips vlan id from the skb
 *  before forwarding to application.
 */
static void tc956xmac_rx_vlan(struct net_device *dev, struct sk_buff *skb)
{
	struct vlan_ethhdr *veth;
	__be16 vlan_proto;
	u16 vlanid;

	veth = (struct vlan_ethhdr *)skb->data;
	vlan_proto = veth->h_vlan_proto;

	if ((vlan_proto == htons(ETH_P_8021Q) &&
	     dev->features & NETIF_F_HW_VLAN_CTAG_RX) ||
	    (vlan_proto == htons(ETH_P_8021AD) &&
	     dev->features & NETIF_F_HW_VLAN_STAG_RX)) {
		/* pop the vlan tag */
		vlanid = ntohs(veth->h_vlan_TCI);
		memmove(skb->data + VLAN_HLEN, veth, ETH_ALEN * 2);
		skb_pull(skb, VLAN_HLEN);
		__vlan_hwaccel_put_tag(skb, vlan_proto, vlanid);
	}
}
#else
/**
 *  tc956xmac_rx_vlan - Rx VLAN Stripping Function
 *  @dev : device pointer
 *  @rdesc : rx descriptor
 *  @skb : the socket buffer
 *  Description : this function extracts vlan id from the descriptor
 *  stripped by MAC VLAN filter.
 */
static void tc956xmac_rx_vlan(struct net_device *dev, 
				struct dma_desc *rdesc,
				struct sk_buff *skb)
{
	u16 vlanid;
	u32 err, etlt;

	/* Check for error in descriptor */
	err = XGMAC_GET_BITS_LE(rdesc->des3, XGMAC_RDES3, ES);
	/* Check for L2 Packet Type Encoding */
	etlt = XGMAC_GET_BITS_LE(rdesc->des3, XGMAC_RDES3, ETLT);
	if (!err) {
		/* No error if err is 0 or etlt is 0 */
		/* Check packet type is Single CVLAN tag and 
		netdev supports VLAN CTAG*/
		if ((etlt == PKT_TYPE_SINGLE_CVLAN) &&
		    (dev->features & NETIF_F_HW_VLAN_CTAG_RX)) {
			vlanid = XGMAC_GET_BITS_LE(rdesc->des0,
							XGMAC_RDES0,
							OVT);
			__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q),
						vlanid);
		}
	}
}
#endif

#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
static inline int tc956xmac_rx_threshold_count(struct tc956xmac_rx_queue *rx_q)
{
	if (rx_q->rx_zeroc_thresh < TC956XMAC_RX_THRESH)
		return 0;

	return 1;
}
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */

/**
 * tc956xmac_rx_refill - refill used skb preallocated buffers
 * @priv: driver private structure
 * @queue: RX queue index
 * Description : this is to reallocate the skb for the reception process
 * that is based on zero-copy.
 */
static inline void tc956xmac_rx_refill(struct tc956xmac_priv *priv, u32 queue)
{
	struct tc956xmac_rx_queue *rx_q = &priv->rx_queue[queue];
	int len, dirty = tc956xmac_rx_dirty(priv, queue);
	unsigned int entry = rx_q->dirty_rx;

	len = DIV_ROUND_UP(priv->dma_buf_sz, PAGE_SIZE) * PAGE_SIZE;

	while (dirty-- > 0) {
		struct tc956xmac_rx_buffer *buf = &rx_q->buf_pool[entry];
		struct dma_desc *p;
		bool use_rx_wd;

		if (priv->extend_desc)
			p = (struct dma_desc *)(rx_q->dma_erx + entry);
		else
			p = rx_q->dma_rx + entry;

		if (!buf->page) {
			buf->page = page_pool_dev_alloc_pages(rx_q->page_pool);
			if (!buf->page)
				break;
		}

		if (priv->sph && !buf->sec_page) {
			buf->sec_page = page_pool_dev_alloc_pages(rx_q->page_pool);
			if (!buf->sec_page)
				break;

			buf->sec_addr = page_pool_get_dma_addr(buf->sec_page);

			dma_sync_single_for_device(priv->device, buf->sec_addr,
						   len, DMA_FROM_DEVICE);
		}

		buf->addr = page_pool_get_dma_addr(buf->page);

		/* Sync whole allocation to device. This will invalidate old
		 * data.
		 */
		dma_sync_single_for_device(priv->device, buf->addr, len,
					   DMA_FROM_DEVICE);

		tc956xmac_set_desc_addr(priv, p, buf->addr);
		tc956xmac_set_desc_sec_addr(priv, p, buf->sec_addr);
		tc956xmac_refill_desc3(priv, rx_q, p);

		rx_q->rx_count_frames++;
		rx_q->rx_count_frames += priv->rx_coal_frames;
		if (rx_q->rx_count_frames > priv->rx_coal_frames)
			rx_q->rx_count_frames = 0;

		use_rx_wd = !priv->rx_coal_frames;
		use_rx_wd |= rx_q->rx_count_frames > 0;
		if (!priv->use_riwt)
			use_rx_wd = false;

		dma_wmb();
		tc956xmac_set_rx_owner(priv, p, use_rx_wd);

		entry = TC956XMAC_GET_ENTRY(entry, DMA_RX_SIZE);
	}
	rx_q->dirty_rx = entry;
	rx_q->rx_tail_addr = rx_q->dma_rx_phy +
			    (rx_q->dirty_rx * sizeof(struct dma_desc));
	tc956xmac_set_rx_tail_ptr(priv, priv->ioaddr, rx_q->rx_tail_addr, queue);
}

static unsigned int tc956xmac_rx_buf1_len(struct tc956xmac_priv *priv,
				       struct dma_desc *p,
				       int status, unsigned int len)
{
	int ret, coe = priv->hw->rx_csum;
	unsigned int plen = 0, hlen = 0;

	/* Not first descriptor, buffer is always zero */
	if (priv->sph && len)
		return 0;

	/* First descriptor, get split header length */
	ret = tc956xmac_get_rx_header_len(priv, p, &hlen);
	if (priv->sph && hlen) {
		priv->xstats.rx_split_hdr_pkt_n++;
		return hlen;
	}

	/* First descriptor, not last descriptor and not split header */
	if (status & rx_not_ls)
		return priv->dma_buf_sz;

	plen = tc956xmac_get_rx_frame_len(priv, p, coe);

	/* First descriptor and last descriptor and not split header */
	return min_t(unsigned int, priv->dma_buf_sz, plen);
}

static unsigned int tc956xmac_rx_buf2_len(struct tc956xmac_priv *priv,
				       struct dma_desc *p,
				       int status, unsigned int len)
{
	int coe = priv->hw->rx_csum;
	unsigned int plen = 0;

	/* Not split header, buffer is not available */
	if (!priv->sph)
		return 0;

	/* Not last descriptor */
	if (status & rx_not_ls)
		return priv->dma_buf_sz;

	plen = tc956xmac_get_rx_frame_len(priv, p, coe);

	/* Last descriptor */
	return plen - len;
}

/**
 * tc956xmac_rx - manage the receive process
 * @priv: driver private structure
 * @limit: napi bugget
 * @queue: RX queue index.
 * Description :  this the function called by the napi poll method.
 * It gets all the frames inside the ring.
 */
static int tc956xmac_rx(struct tc956xmac_priv *priv, int limit, u32 queue)
{
	struct tc956xmac_rx_queue *rx_q = &priv->rx_queue[queue];
	struct tc956xmac_channel *ch = &priv->channel[queue];
	unsigned int count = 0, error = 0, len = 0, per_queue_count = 0;
	int status = 0, coe = priv->hw->rx_csum;
	unsigned int next_entry = rx_q->cur_rx;
	struct sk_buff *skb = NULL;
	unsigned int proto;

	if (netif_msg_rx_status(priv)) {
		void *rx_head;

		netdev_dbg(priv->dev, "%s: descriptor ring:\n", __func__);
		if (priv->extend_desc)
			rx_head = (void *)rx_q->dma_erx;
		else
			rx_head = (void *)rx_q->dma_rx;

		tc956xmac_display_ring(priv, rx_head, DMA_RX_SIZE, true);
	}
	while (count < limit) {
		unsigned int buf1_len = 0, buf2_len = 0;
		enum pkt_hash_types hash_type;
		struct tc956xmac_rx_buffer *buf;
		struct dma_desc *np, *p;
		int entry;
		u32 hash;

		if (!count && rx_q->state_saved) {
			skb = rx_q->state.skb;
			error = rx_q->state.error;
			len = rx_q->state.len;
		} else {
			rx_q->state_saved = false;
			skb = NULL;
			error = 0;
			len = 0;
		}

		if (count >= limit)
			break;

read_again:
		buf1_len = 0;
		buf2_len = 0;
		entry = next_entry;
		buf = &rx_q->buf_pool[entry];

		if (priv->extend_desc)
			p = (struct dma_desc *)(rx_q->dma_erx + entry);
		else
			p = rx_q->dma_rx + entry;

		/* read the status of the incoming frame */
		status = tc956xmac_rx_status(priv, &priv->dev->stats,
				&priv->xstats, p);
		/* check if managed by the DMA otherwise go ahead */
		if (unlikely(status & dma_own))
			break;

		rx_q->cur_rx = TC956XMAC_GET_ENTRY(rx_q->cur_rx, DMA_RX_SIZE);
		next_entry = rx_q->cur_rx;

		if (priv->extend_desc)
			np = (struct dma_desc *)(rx_q->dma_erx + next_entry);
		else
			np = rx_q->dma_rx + next_entry;

		prefetch(np);

		if (priv->extend_desc)
			tc956xmac_rx_extended_status(priv, &priv->dev->stats,
					&priv->xstats, rx_q->dma_erx + entry);
		if (unlikely(status == discard_frame)) {
			page_pool_recycle_direct(rx_q->page_pool, buf->page);
			buf->page = NULL;
			error = 1;
			if (!priv->hwts_rx_en)
				priv->dev->stats.rx_errors++;
		}

		if (unlikely(error && (status & rx_not_ls)))
			goto read_again;
		if (unlikely(error)) {
			dev_kfree_skb(skb);
			skb = NULL;
			count++;
			continue;
		}

		/* Buffer is good. Go on. */

		prefetch(page_address(buf->page));
		if (buf->sec_page)
			prefetch(page_address(buf->sec_page));

		buf1_len = tc956xmac_rx_buf1_len(priv, p, status, len);
		len += buf1_len;
		buf2_len = tc956xmac_rx_buf2_len(priv, p, status, len);
		len += buf2_len;

		/* CRC stripping is done in MAC (CST &ACS bits) based on ethtool state*/
		if (!skb) {
			skb = napi_alloc_skb(&ch->rx_napi, buf1_len);
			if (!skb) {
				priv->dev->stats.rx_dropped++;
				count++;
				goto drain_data;
			}

			dma_sync_single_for_cpu(priv->device, buf->addr,
						buf1_len, DMA_FROM_DEVICE);
			skb_copy_to_linear_data(skb, page_address(buf->page),
						buf1_len);
			skb_put(skb, buf1_len);

			/* Data payload copied into SKB, page ready for recycle */
			page_pool_recycle_direct(rx_q->page_pool, buf->page);
			buf->page = NULL;
		} else if (buf1_len) {
			dma_sync_single_for_cpu(priv->device, buf->addr,
						buf1_len, DMA_FROM_DEVICE);
			skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags,
					buf->page, 0, buf1_len,
					priv->dma_buf_sz);

			/* Data payload appended into SKB */
			page_pool_release_page(rx_q->page_pool, buf->page);
			buf->page = NULL;
		}

		if (buf2_len) {
			dma_sync_single_for_cpu(priv->device, buf->sec_addr,
						buf2_len, DMA_FROM_DEVICE);
			skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags,
					buf->sec_page, 0, buf2_len,
					priv->dma_buf_sz);

			/* Data payload appended into SKB */
			page_pool_release_page(rx_q->page_pool, buf->sec_page);
			buf->sec_page = NULL;
		}

drain_data:
		if (likely(status & rx_not_ls))
			goto read_again;
		if (!skb)
			continue;

		/* Got entire packet into SKB. Finish it. */
		/* Pause frame counter to count link partner pause frames */
		if ((mac0_en_lp_pause_frame_cnt == ENABLE && priv->port_num == RM_PF0_ID) ||
			(mac1_en_lp_pause_frame_cnt == ENABLE && priv->port_num == RM_PF1_ID)) {
			proto = htons(((skb->data[13]<<8) | skb->data[12]));
			if (proto == ETH_P_PAUSE) {
				if(!(skb->data[6] == phy_sa_addr[priv->port_num][0] && skb->data[7] == phy_sa_addr[priv->port_num][1] 
					&& skb->data[8] == phy_sa_addr[priv->port_num][2] && skb->data[9] == phy_sa_addr[priv->port_num][3]
					&& skb->data[10] == phy_sa_addr[priv->port_num][4] && skb->data[11] == phy_sa_addr[priv->port_num][5])) {
					priv->xstats.link_partner_pause_frame_cnt++;
				}
			}
		}

		tc956xmac_get_rx_hwtstamp(priv, p, np, skb);
#ifndef TC956X
		tc956xmac_rx_vlan(priv->dev, skb);
#else
		tc956xmac_rx_vlan(priv->dev, p, skb);
#endif
		skb->protocol = eth_type_trans(skb, priv->dev);

		if (unlikely(!coe))
			skb_checksum_none_assert(skb);
		else
			skb->ip_summed = CHECKSUM_UNNECESSARY;

		if (!tc956xmac_get_rx_hash(priv, p, &hash, &hash_type))
			skb_set_hash(skb, hash, hash_type);

		skb_record_rx_queue(skb, queue);
		napi_gro_receive(&ch->rx_napi, skb);
		skb = NULL;

		priv->dev->stats.rx_packets++;
		priv->dev->stats.rx_bytes += len;
		count++;
		per_queue_count++;
	}

	if (status & rx_not_ls || skb) {
		rx_q->state_saved = true;
		rx_q->state.skb = skb;
		rx_q->state.error = error;
		rx_q->state.len = len;
	}

	tc956xmac_rx_refill(priv, queue);

	/* priv->xstats.rx_pkt_n[queue]+= count; */
	/* Count only Acceptd packet */
	priv->xstats.rx_pkt_n[queue] += per_queue_count;


	return count;
}

static int tc956xmac_napi_poll_rx(struct napi_struct *napi, int budget)
{
	struct tc956xmac_channel *ch =
		container_of(napi, struct tc956xmac_channel, rx_napi);
	struct tc956xmac_priv *priv = ch->priv_data;
	u32 chan = ch->index;
	int work_done;

	priv->xstats.napi_poll_rx[chan]++;

	work_done = tc956xmac_rx(priv, budget, chan);
	if (work_done < budget && napi_complete_done(napi, work_done)) {
		unsigned long flags;

		spin_lock_irqsave(&ch->lock, flags);
		tc956xmac_enable_dma_irq(priv, priv->ioaddr, chan, 1, 0);
		spin_unlock_irqrestore(&ch->lock, flags);
	}

	return work_done;
}

static int tc956xmac_napi_poll_tx(struct napi_struct *napi, int budget)
{
	struct tc956xmac_channel *ch =
		container_of(napi, struct tc956xmac_channel, tx_napi);
	struct tc956xmac_priv *priv = ch->priv_data;
	u32 chan = ch->index;
	int work_done;

	priv->xstats.napi_poll_tx[chan]++;

	work_done = tc956xmac_tx_clean(priv, DMA_TX_SIZE, chan);
	work_done = min(work_done, budget);

	if (work_done < budget && napi_complete_done(napi, work_done)) {
		unsigned long flags;

		spin_lock_irqsave(&ch->lock, flags);
		tc956xmac_enable_dma_irq(priv, priv->ioaddr, chan, 0, 1);
		spin_unlock_irqrestore(&ch->lock, flags);
	}

	return work_done;
}

/**
 *  tc956xmac_tx_timeout
 *  @dev : Pointer to net device structure
 *  Description: this function is called when a packet transmission fails to
 *   complete within a reasonable time. The driver will mark the error in the
 *   netdev structure and arrange for the device to be reset to a sane state
 *   in order to transmit a new packet.
 */
static void tc956xmac_tx_timeout(struct net_device *dev)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);

	tc956xmac_global_err(priv);
}

/**
 *  tc956xmac_set_rx_mode - entry point for multicast addressing
 *  @dev : pointer to the device structure
 *  Description:
 *  This function is a driver entry point which gets called by the kernel
 *  whenever multicast addresses must be enabled/disabled.
 *  Return value:
 *  void.
 */
static void tc956xmac_set_rx_mode(struct net_device *dev)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);

	tc956xmac_set_filter(priv, priv->hw, dev);
}

/**
 *  tc956xmac_change_mtu - entry point to change MTU size for the device.
 *  @dev : device pointer.
 *  @new_mtu : the new MTU size for the device.
 *  Description: the Maximum Transfer Unit (MTU) is used by the network layer
 *  to drive packet transmission. Ethernet has an MTU of 1500 octets
 *  (ETH_DATA_LEN). This value can be changed with ifconfig.
 *  Return value:
 *  0 on success and an appropriate (-)ve integer as defined in errno.h
 *  file on failure.
 */
static int tc956xmac_change_mtu(struct net_device *dev, int new_mtu)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
	int txfifosz = priv->plat->tx_fifo_size;


	if (txfifosz == 0)
		txfifosz = priv->dma_cap.tx_fifo_size;

	txfifosz /= priv->plat->tx_queues_to_use;

	if (netif_running(dev)) {
		netdev_err(priv->dev, "must be stopped to change its MTU\n");
		return -EBUSY;
	}

	new_mtu = TC956XMAC_ALIGN(new_mtu);
#ifdef TC956X
	/* Supported frame sizes */
	if ((new_mtu < MIN_SUPPORTED_MTU) || (new_mtu > JUMBO_LEN)) {
		NMSGPR_ALERT(priv->device,
		       "%s: invalid MTU, min %d and max %d MTU are supported\n",
		       dev->name, MIN_SUPPORTED_MTU, JUMBO_LEN);
		return -EINVAL;
	}

#else
	/* If condition true, FIFO is too small or MTU too large */
	if ((txfifosz < new_mtu) || (new_mtu > BUF_SIZE_16KiB))
		return -EINVAL;
#endif
	dev->mtu = new_mtu;

	netdev_update_features(dev);

	return 0;
}

static netdev_features_t tc956xmac_fix_features(struct net_device *dev,
					     netdev_features_t features)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);

	if (priv->plat->rx_coe == TC956XMAC_RX_COE_NONE)
		features &= ~NETIF_F_RXCSUM;

	if (!priv->plat->tx_coe)
		features &= ~NETIF_F_CSUM_MASK;

	/* Some GMAC devices have a bugged Jumbo frame support that
	 * needs to have the Tx COE disabled for oversized frames
	 * (due to limited buffer sizes). In this case we disable
	 * the TX csum insertion in the TDES and not use SF.
	 */
	if (priv->plat->bugged_jumbo && (dev->mtu > ETH_DATA_LEN))
		features &= ~NETIF_F_CSUM_MASK;

	/* Disable tso if asked by ethtool */
	if ((priv->plat->tso_en) && (priv->dma_cap.tsoen)) {
		if (features & NETIF_F_TSO)
			priv->tso = true;
		else
			priv->tso = false;
	}

	return features;
}

static int tc956xmac_set_features(struct net_device *netdev,
			       netdev_features_t features)
{
	struct tc956xmac_priv *priv = netdev_priv(netdev);
	bool sph_en;
	u32 chan;
#ifdef TC956X
	netdev_features_t txvlan, rxvlan, rxvlan_filter;

	rxvlan = (netdev->features & NETIF_F_HW_VLAN_CTAG_RX);
	txvlan = (netdev->features & NETIF_F_HW_VLAN_CTAG_TX);
	rxvlan_filter = (netdev->features & NETIF_F_HW_VLAN_CTAG_FILTER);
#endif
	/* Keep the COE Type in case of csum is supporting */
	if (features & NETIF_F_RXCSUM)
		priv->hw->rx_csum = priv->plat->rx_coe;
	else
		priv->hw->rx_csum = 0;
	/* No check needed because rx_coe has been set before and it will be
	 * fixed in case of issue.
	 */
	tc956xmac_rx_ipc(priv, priv->hw);

	/* Tx Checksum Configuration via Ethtool */
	if ((features & NETIF_F_IP_CSUM) || (features & NETIF_F_IPV6_CSUM))
		priv->csum_insertion = 1;
	else
		priv->csum_insertion = 0;

	KPRINT_DEBUG1("priv->csum_insertion = %d\n", priv->csum_insertion);
	/* Rx fcs Configuration via Ethtool */
	if (features & NETIF_F_RXFCS) {
		priv->rx_crc_pad_state &= (~TC956X_RX_CRC_DEFAULT);
	} else {
#ifdef TC956X
		priv->rx_crc_pad_state = TC956X_RX_CRC_DEFAULT;
#endif
	}

	tc956x_rx_crc_pad_config(priv, priv->rx_crc_pad_state);
	KPRINT_DEBUG1("priv->rx_crc_pad_state = %d\n", priv->rx_crc_pad_state);

	sph_en = (priv->hw->rx_csum > 0) && priv->sph;
	for (chan = 0; chan < priv->plat->rx_queues_to_use; chan++)
		tc956xmac_enable_sph(priv, priv->ioaddr, sph_en, chan);

#ifdef TC956X
	if ((features & NETIF_F_HW_VLAN_CTAG_RX) && !rxvlan)
		tc956xmac_enable_rx_vlan_stripping(priv, priv->hw);
	else if (!(features & NETIF_F_HW_VLAN_CTAG_RX) && rxvlan)
		tc956xmac_disable_rx_vlan_stripping(priv, priv->hw);

	if ((features & NETIF_F_HW_VLAN_CTAG_TX) && !txvlan)
		tc956xmac_enable_vlan(priv, priv->hw, TC956XMAC_VLAN_INSERT);
	else if (!(features & NETIF_F_HW_VLAN_CTAG_TX) && txvlan)
		tc956xmac_disable_tx_vlan(priv, priv->hw);

	if ((features & NETIF_F_HW_VLAN_CTAG_FILTER) && !rxvlan_filter)
		tc956xmac_enable_rx_vlan_filtering(priv, priv->hw);
	else if (!(features & NETIF_F_HW_VLAN_CTAG_FILTER) && rxvlan_filter)
		tc956xmac_disable_rx_vlan_filtering(priv, priv->hw);
#endif
	return 0;
}

/**
 *  tc956xmac_interrupt - main ISR
 *  @irq: interrupt number.
 *  @dev_id: to pass the net device pointer.
 *  Description: this is the main driver interrupt service routine.
 *  It can call:
 *  o DMA service routine (to manage incoming frame reception and transmission
 *    status)
 *  o Core interrupts to manage: remote wake-up, management counter, LPI
 *    interrupts.
 */
static irqreturn_t tc956xmac_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct tc956xmac_priv *priv = netdev_priv(dev);
#ifdef TC956X
	u32 rx_cnt = priv->plat->rx_queues_to_use;
	u32 tx_cnt = priv->plat->tx_queues_to_use;
#endif
	u32 queues_count;
	u32 queue;
	bool xmac;
	u32 val = 0;
	uint32_t uiIntSts, uiIntclr = 0;

	xmac = priv->plat->has_gmac4 || priv->plat->has_xgmac;
#ifdef TC956X
	queues_count = (rx_cnt > tx_cnt) ? rx_cnt : tx_cnt;
#endif
	if (priv->irq_wake)
		pm_wakeup_event(priv->device, 0);

	if (unlikely(!dev)) {
		netdev_err(priv->dev, "%s: invalid dev pointer\n", __func__);
		return IRQ_NONE;
	}

	/* Check if adapter is up */
	if (test_bit(TC956XMAC_DOWN, &priv->state))
		return IRQ_HANDLED;
	/* Check if a fatal error happened */
	if (tc956xmac_safety_feat_interrupt(priv))
		return IRQ_HANDLED;

	val = readl(priv->ioaddr + TC956X_MSI_INT_STS_OFFSET(priv->port_num));

	priv->xstats.total_interrupts++;

	if (val & (1 << 0))
		priv->xstats.lpi_intr_n++;

	if (val & (1 << 1))
		priv->xstats.pmt_intr_n++;

	if (val & (1 << 2))
		priv->xstats.event_intr_n++;

	if (val & (0xFF << 3))
		priv->xstats.tx_intr_n++;

	if (val & (0xFF << 11))
		priv->xstats.rx_intr_n++;

	if (val & (1 << 19))
		priv->xstats.xpcs_intr_n++;

	if (val & (1 << 20))
		priv->xstats.phy_intr_n++;

	if (val & (1 << 24))
		priv->xstats.sw_msi_n++;

	/* Checking if any RBUs occurred and updating the statistics corresponding to channel */

	for (queue = 0; queue < queues_count; queue++) {
		uiIntSts = readl(priv->ioaddr + XGMAC_DMA_CH_STATUS(queue));
		if(uiIntSts & XGMAC_RBU) {
			priv->xstats.rx_buf_unav_irq[queue]++;
			uiIntclr |= XGMAC_RBU;
		}
		writel(uiIntclr, (priv->ioaddr + XGMAC_DMA_CH_STATUS(queue)));
	}

	/* To handle GMAC own interrupts */
	if ((priv->plat->has_gmac) || xmac) {
		int status = tc956xmac_host_irq_status(priv, priv->hw, &priv->xstats);
		int mtl_status;

		if (unlikely(status)) {
			/* For LPI we need to save the tx status */
			if (status & CORE_IRQ_TX_PATH_IN_LPI_MODE)
				priv->tx_path_in_lpi_mode = true;
			if (status & CORE_IRQ_TX_PATH_EXIT_LPI_MODE)
				priv->tx_path_in_lpi_mode = false;
		}

		for (queue = 0; queue < queues_count; queue++) {
			struct tc956xmac_rx_queue *rx_q = &priv->rx_queue[queue];

			mtl_status = tc956xmac_host_mtl_irq_status(priv, priv->hw,
								queue);
			if (mtl_status != -EINVAL)
				status |= mtl_status;

			if (status & CORE_IRQ_MTL_RX_OVERFLOW)
				tc956xmac_set_rx_tail_ptr(priv, priv->ioaddr,
						       rx_q->rx_tail_addr,
						       queue);
		}

		/* PCS link status */
		if (priv->hw->pcs) {
			if (priv->xstats.pcs_link)
				netif_carrier_on(dev);
			else
				netif_carrier_off(dev);
		}
	}

	/* To handle DMA interrupts */
	tc956xmac_dma_interrupt(priv);

	val = readl(priv->ioaddr + TC956X_MSI_INT_STS_OFFSET(priv->port_num));
	if (val & TC956X_EXT_PHY_ETH_INT) {
		KPRINT_INFO("PHY Interrupt %s \n", __func__);
		/* Queue the work in system_wq */
		if (priv->tc956x_port_pm_suspend == true) {
			KPRINT_INFO("%s : (Do not queue PHY Work during suspend. Set WOL Interrupt flag) \n", __func__);
			priv->tc956xmac_pm_wol_interrupt = true;
		} else {
			KPRINT_INFO("%s : (Queue PHY Work.) \n", __func__);
			queue_work(system_wq, &priv->emac_phy_work);
		}
		/* phy_mac_interrupt(priv->dev->phydev); */
		val = readl(priv->ioaddr + TC956X_MSI_OUT_EN_OFFSET(priv->port_num)); /* MSI_OUT_EN: Reading */
		val &= (~(1 << MSI_INT_EXT_PHY));
		writel(val, priv->ioaddr + TC956X_MSI_OUT_EN_OFFSET(priv->port_num)); /* MSI_OUT_EN: Writing to disable MAC Ext Interrupt*/
	}
#ifdef TC956X_SW_MSI
	if (val & TC956X_SW_MSI_INT) {
		//DBGPR_FUNC(priv->device, "%s SW MSI INT STS[%08x]\n", __func__, val);

		/*Clear SW MSI*/
		writel(1, priv->ioaddr + TC956X_MSI_SW_MSI_CLR(priv->port_num));

		val = readl(priv->ioaddr + TC956X_MSI_SW_MSI_CLR(priv->port_num));

		//DBGPR_FUNC(priv->device, "%s SW MSI INT CLR[%08x]\n", __func__, val);

	}
#endif

#ifdef TC956X
	/* MSI_MSK_CLR, unmask vector 0 */
	writel(0x1, priv->ioaddr + TC956X_MSI_MASK_CLR_OFFSET(priv->port_num));
#endif

	return IRQ_HANDLED;
}

#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
#ifdef CONFIG_NET_POLL_CONTROLLER
/* Polling receive - used by NETCONSOLE and other diagnostic tools
 * to allow network I/O with interrupts disabled.
 */
static void tc956xmac_poll_controller(struct net_device *dev)
{
	disable_irq(dev->irq);
	tc956xmac_interrupt(dev->irq, dev);
	enable_irq(dev->irq);
}
#endif
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */

int tc956xmac_rx_parser_configuration(struct tc956xmac_priv *priv)
{
	int ret = -EINVAL;

	if (priv->hw->mac->rx_parser_init && priv->plat->rxp_cfg.enable)
		ret = tc956xmac_rx_parser_init(priv,
			priv->dev, priv->hw, priv->dma_cap.spram,
			priv->dma_cap.frpsel, priv->dma_cap.frpes,
			&priv->plat->rxp_cfg);

		/* spram feautre is not present in TC956X */
	if (ret)
		priv->rxp_enabled = false;
	else
		priv->rxp_enabled = true;

	return ret;
}
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
static int tc956xmac_est_configuration_ioctl(struct tc956xmac_priv *priv)
{
	int ret = -EINVAL;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

#ifdef TC956X
	ret = tc956xmac_est_configure(priv, priv->ioaddr, priv->plat->est,
				   priv->plat->clk_ptp_rate);
#endif

	DBGPR_FUNC(priv->device, "<--%s\n", __func__);

	return ret;
}

/**
 * tc956xmac_ioctl_get_cbs - gets the cbs parameter
 * @priv: driver private structure
 * @data: tc956xmac_ioctl_cbs_cfg strcuture passed by user
 * Note: percentage element is dummy parameter and is not used in code.
 * Description :  this function gets called when ioctl TC956XMAC_IOTL_GET_CBS
 * is invoked.
 * As pre-requisite for calling this function, CBS Set must be done before get.
 *
 */
static int tc956xmac_ioctl_get_cbs(struct tc956xmac_priv *priv, void __user *data)
{
	u32 tx_qcount = priv->plat->tx_queues_to_use;
	struct tc956xmac_ioctl_cbs_cfg cbs;
	u8 qmode;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	if (copy_from_user(&cbs, data, sizeof(cbs)))
		return -EFAULT;

	/* queue 0 is reserved for legacy traffic; cbs configuration not allowed (registers also not available for Q0)*/
	if ((cbs.queue_idx >= tx_qcount) || (cbs.queue_idx == 0))
		return -EINVAL;

	qmode = priv->plat->tx_queues_cfg[cbs.queue_idx].mode_to_use;

	/* Only AVB queue supported for cbs */
	if (qmode != MTL_QUEUE_AVB)
		return -EINVAL;

	cbs.speed100cfg.send_slope = priv->cbs_speed100_cfg[cbs.queue_idx].send_slope;
	cbs.speed100cfg.idle_slope = priv->cbs_speed100_cfg[cbs.queue_idx].idle_slope;
	cbs.speed100cfg.high_credit = priv->cbs_speed100_cfg[cbs.queue_idx].high_credit;
	cbs.speed100cfg.low_credit = priv->cbs_speed100_cfg[cbs.queue_idx].low_credit;

	cbs.speed1000cfg.send_slope = priv->cbs_speed1000_cfg[cbs.queue_idx].send_slope;
	cbs.speed1000cfg.idle_slope = priv->cbs_speed1000_cfg[cbs.queue_idx].idle_slope;
	cbs.speed1000cfg.high_credit = priv->cbs_speed1000_cfg[cbs.queue_idx].high_credit;
	cbs.speed1000cfg.low_credit = priv->cbs_speed1000_cfg[cbs.queue_idx].low_credit;

	cbs.speed10000cfg.send_slope = priv->cbs_speed10000_cfg[cbs.queue_idx].send_slope;
	cbs.speed10000cfg.idle_slope = priv->cbs_speed10000_cfg[cbs.queue_idx].idle_slope;
	cbs.speed10000cfg.high_credit = priv->cbs_speed10000_cfg[cbs.queue_idx].high_credit;
	cbs.speed10000cfg.low_credit = priv->cbs_speed10000_cfg[cbs.queue_idx].low_credit;

	cbs.speed5000cfg.send_slope = priv->cbs_speed5000_cfg[cbs.queue_idx].send_slope;
	cbs.speed5000cfg.idle_slope = priv->cbs_speed5000_cfg[cbs.queue_idx].idle_slope;
	cbs.speed5000cfg.high_credit = priv->cbs_speed5000_cfg[cbs.queue_idx].high_credit;
	cbs.speed5000cfg.low_credit = priv->cbs_speed5000_cfg[cbs.queue_idx].low_credit;

	cbs.speed2500cfg.send_slope = priv->cbs_speed2500_cfg[cbs.queue_idx].send_slope;
	cbs.speed2500cfg.idle_slope = priv->cbs_speed2500_cfg[cbs.queue_idx].idle_slope;
	cbs.speed2500cfg.high_credit = priv->cbs_speed2500_cfg[cbs.queue_idx].high_credit;
	cbs.speed2500cfg.low_credit = priv->cbs_speed2500_cfg[cbs.queue_idx].low_credit;
	if (copy_to_user(data, &cbs, sizeof(cbs)))
		return -EFAULT;

	DBGPR_FUNC(priv->device, "<--%s\n", __func__);

	return 0;
}

static int tc956xmac_ioctl_set_cbs(struct tc956xmac_priv *priv, void __user *data)
{
	u32 tx_qcount = priv->plat->tx_queues_to_use;
	struct tc956xmac_ioctl_cbs_cfg cbs;
	u8 qmode;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	if (copy_from_user(&cbs, data, sizeof(cbs)))
		return -EFAULT;

	/* queue 0 is reserved for legacy traffic; cbs configuration not allowed (registers also not available for Q0)*/
	if ((cbs.queue_idx >= tx_qcount) || (cbs.queue_idx == 0))
		return -EINVAL;

	if (!priv->hw->mac->config_cbs)
		return -EINVAL;

	qmode = priv->plat->tx_queues_cfg[cbs.queue_idx].mode_to_use;

	if (qmode != MTL_QUEUE_AVB)
		return -EINVAL;

	priv->cbs_speed100_cfg[cbs.queue_idx].send_slope = cbs.speed100cfg.send_slope;
	priv->cbs_speed100_cfg[cbs.queue_idx].idle_slope = cbs.speed100cfg.idle_slope;
	priv->cbs_speed100_cfg[cbs.queue_idx].high_credit = cbs.speed100cfg.high_credit;
	priv->cbs_speed100_cfg[cbs.queue_idx].low_credit = cbs.speed100cfg.low_credit;

	priv->cbs_speed1000_cfg[cbs.queue_idx].send_slope = cbs.speed1000cfg.send_slope;
	priv->cbs_speed1000_cfg[cbs.queue_idx].idle_slope = cbs.speed1000cfg.idle_slope;
	priv->cbs_speed1000_cfg[cbs.queue_idx].high_credit = cbs.speed1000cfg.high_credit;
	priv->cbs_speed1000_cfg[cbs.queue_idx].low_credit = cbs.speed1000cfg.low_credit;

	priv->cbs_speed10000_cfg[cbs.queue_idx].send_slope = cbs.speed10000cfg.send_slope;
	priv->cbs_speed10000_cfg[cbs.queue_idx].idle_slope = cbs.speed10000cfg.idle_slope;
	priv->cbs_speed10000_cfg[cbs.queue_idx].high_credit = cbs.speed10000cfg.high_credit;
	priv->cbs_speed10000_cfg[cbs.queue_idx].low_credit = cbs.speed10000cfg.low_credit;

	priv->cbs_speed2500_cfg[cbs.queue_idx].send_slope = cbs.speed2500cfg.send_slope;
	priv->cbs_speed2500_cfg[cbs.queue_idx].idle_slope = cbs.speed2500cfg.idle_slope;
	priv->cbs_speed2500_cfg[cbs.queue_idx].high_credit = cbs.speed2500cfg.high_credit;
	priv->cbs_speed2500_cfg[cbs.queue_idx].low_credit = cbs.speed2500cfg.low_credit;


	priv->cbs_speed5000_cfg[cbs.queue_idx].send_slope = cbs.speed5000cfg.send_slope;
	priv->cbs_speed5000_cfg[cbs.queue_idx].idle_slope = cbs.speed5000cfg.idle_slope;
	priv->cbs_speed5000_cfg[cbs.queue_idx].high_credit = cbs.speed5000cfg.high_credit;
	priv->cbs_speed5000_cfg[cbs.queue_idx].low_credit = cbs.speed5000cfg.low_credit;

	if (priv->speed == SPEED_100) {
		priv->plat->tx_queues_cfg[cbs.queue_idx].send_slope =
								priv->cbs_speed100_cfg[cbs.queue_idx].send_slope;
		priv->plat->tx_queues_cfg[cbs.queue_idx].idle_slope =
								priv->cbs_speed100_cfg[cbs.queue_idx].idle_slope;
		priv->plat->tx_queues_cfg[cbs.queue_idx].high_credit =
								priv->cbs_speed100_cfg[cbs.queue_idx].high_credit;
		priv->plat->tx_queues_cfg[cbs.queue_idx].low_credit =
								priv->cbs_speed100_cfg[cbs.queue_idx].low_credit;
	} else if (priv->speed == SPEED_1000) {
		priv->plat->tx_queues_cfg[cbs.queue_idx].send_slope =
								priv->cbs_speed1000_cfg[cbs.queue_idx].send_slope;
		priv->plat->tx_queues_cfg[cbs.queue_idx].idle_slope =
								priv->cbs_speed1000_cfg[cbs.queue_idx].idle_slope;
		priv->plat->tx_queues_cfg[cbs.queue_idx].high_credit =
								priv->cbs_speed1000_cfg[cbs.queue_idx].high_credit;
		priv->plat->tx_queues_cfg[cbs.queue_idx].low_credit =
								priv->cbs_speed1000_cfg[cbs.queue_idx].low_credit;
	} else if (priv->speed == SPEED_10000) {
		priv->plat->tx_queues_cfg[cbs.queue_idx].send_slope =
								priv->cbs_speed10000_cfg[cbs.queue_idx].send_slope;
		priv->plat->tx_queues_cfg[cbs.queue_idx].idle_slope =
								priv->cbs_speed10000_cfg[cbs.queue_idx].idle_slope;
		priv->plat->tx_queues_cfg[cbs.queue_idx].high_credit =
								priv->cbs_speed10000_cfg[cbs.queue_idx].high_credit;
		priv->plat->tx_queues_cfg[cbs.queue_idx].low_credit =
								priv->cbs_speed10000_cfg[cbs.queue_idx].low_credit;
	} else if (priv->speed == SPEED_2500) {
		priv->plat->tx_queues_cfg[cbs.queue_idx].send_slope =
								priv->cbs_speed2500_cfg[cbs.queue_idx].send_slope;
		priv->plat->tx_queues_cfg[cbs.queue_idx].idle_slope =
								priv->cbs_speed2500_cfg[cbs.queue_idx].idle_slope;
		priv->plat->tx_queues_cfg[cbs.queue_idx].high_credit =
								priv->cbs_speed2500_cfg[cbs.queue_idx].high_credit;
		priv->plat->tx_queues_cfg[cbs.queue_idx].low_credit =
								priv->cbs_speed2500_cfg[cbs.queue_idx].low_credit;
	} else if (priv->speed == SPEED_5000) {
		priv->plat->tx_queues_cfg[cbs.queue_idx].send_slope =
								priv->cbs_speed5000_cfg[cbs.queue_idx].send_slope;
		priv->plat->tx_queues_cfg[cbs.queue_idx].idle_slope =
								priv->cbs_speed5000_cfg[cbs.queue_idx].idle_slope;
		priv->plat->tx_queues_cfg[cbs.queue_idx].high_credit =
								priv->cbs_speed5000_cfg[cbs.queue_idx].high_credit;
		priv->plat->tx_queues_cfg[cbs.queue_idx].low_credit =
								priv->cbs_speed5000_cfg[cbs.queue_idx].low_credit;
	}

	tc956xmac_config_cbs(priv, priv->hw, priv->plat->tx_queues_cfg[cbs.queue_idx].send_slope,
				priv->plat->tx_queues_cfg[cbs.queue_idx].idle_slope,
				priv->plat->tx_queues_cfg[cbs.queue_idx].high_credit,
				priv->plat->tx_queues_cfg[cbs.queue_idx].low_credit,
				cbs.queue_idx);

	DBGPR_FUNC(priv->device, "<--%s\n", __func__);

	return 0;
}

static int tc956xmac_ioctl_get_est(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956xmac_ioctl_est_cfg *est;
	int ret = 0;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	est = kzalloc(sizeof(*est), GFP_KERNEL);
	if (!est)
		return -ENOMEM;

	est->enabled = priv->plat->est->enable;
	est->estwid = priv->dma_cap.estwid;
	est->estdep = priv->dma_cap.estdep;
	est->btr_offset[0] = priv->plat->est->btr_offset[0];
	est->btr_offset[1] = priv->plat->est->btr_offset[1];
	est->ctr[0] = priv->plat->est->ctr[0];
	est->ctr[1] = priv->plat->est->ctr[1];
	est->ter = priv->plat->est->ter;
	est->gcl_size = priv->plat->est->gcl_size;
	memcpy(est->gcl, priv->plat->est->gcl, est->gcl_size * sizeof(*est->gcl));
	if (copy_to_user(data, est, sizeof(*est))) {
		ret = -EFAULT;
		goto out_free;
	}

	DBGPR_FUNC(priv->device, "<--%s\n", __func__);

out_free:
	kfree(est);

	return ret;
}

static int tc956xmac_ioctl_set_est(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956xmac_est *cfg = priv->plat->est;
	struct tc956xmac_ioctl_est_cfg *est;
	int ret = 0;
#ifdef TC956X
	u64 system_time;
	u32 system_time_s;
	u32 system_time_ns;
#ifndef CONFIG_ARCH_DMA_ADDR_T_64BIT
	u64 quotient;
	u32 reminder;
#endif

#endif

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	est = kzalloc(sizeof(*est), GFP_KERNEL);
	if (!est)
		return -ENOMEM;

	if (copy_from_user(est, data, sizeof(*est))) {
		ret = -EFAULT;
		goto out_free;
	}
#ifdef TC956X
	if (est->gcl_size > TC956XMAC_IOCTL_EST_GCL_MAX_ENTRIES) {
		ret = -EINVAL;
		goto out_free;
	}
#endif
	if (est->enabled) {
		cfg->btr_offset[0] = est->btr_offset[0];
		cfg->btr_offset[1] = est->btr_offset[1];
		cfg->ctr[0] = est->ctr[0];
		cfg->ctr[1] = est->ctr[1];
		cfg->ter = est->ter;
		cfg->gcl_size = est->gcl_size;
#ifdef TC956X
		/* BTR Offset */
		tc956xmac_get_systime(priv, priv->ptpaddr, &system_time);
#ifndef CONFIG_ARCH_DMA_ADDR_T_64BIT
		quotient = div_u64_rem(system_time, 1000000000ULL, &reminder);
		system_time_s = (u32)quotient;
		system_time_ns = reminder;
#else
		system_time_s = system_time / 1000000000;
		system_time_ns = system_time % 1000000000;
#endif
		cfg->btr[0] = cfg->btr_offset[0] + (u32)system_time_ns;
		cfg->btr[1] = cfg->btr_offset[1] + (u32)system_time_s;
#endif
		memcpy(cfg->gcl, est->gcl, cfg->gcl_size * sizeof(*cfg->gcl));
	} else {
		cfg->btr_offset[0] = 0;
		cfg->btr_offset[1] = 0;
		cfg->ctr[0] = 0;
		cfg->ctr[1] = 0;
		cfg->ter = 0;
		cfg->gcl_size = 0;
		memset(cfg->gcl, 0, sizeof(cfg->gcl));
	}

	cfg->enable = est->enabled;
	tc956xmac_est_configuration_ioctl(priv);

	if (!est->enabled)
		ret = 0;

	DBGPR_FUNC(priv->device, "<--%s\n", __func__);

out_free:
	kfree(est);
	return ret;
}

static int tc956xmac_ioctl_get_fpe(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956xmac_ioctl_fpe_cfg *fpe;
	int ret = 0;
	unsigned int control = 0;

	fpe = kzalloc(sizeof(*fpe), GFP_KERNEL);

	if (!fpe)
		return -ENOMEM;
#ifdef TC956X
	control = readl(priv->ioaddr + XGMAC_FPE_CTRL_STS);
#endif

	if (control & XGMAC_EFPE) {
		fpe->enabled = 1;
#ifdef TC956X
		control = readl(priv->ioaddr + XGMAC_MTL_FPE_CTRL_STS);
		fpe->pec = (control & XGMAC_MTL_FPE_PEC_MASK) >> XGMAC_MTL_FPE_PEC_SHIFT;
		fpe->afsz = control & XGMAC_MTL_FPE_AFSZ_MASK;
		control = readl(priv->ioaddr + XGMAC_MTL_FPE_ADVANCE);
		fpe->HA_time =  (control & XGMAC_MTL_FPE_HOLD_ADVANCE_MASK);
		fpe->RA_time =  (control & XGMAC_MTL_FPE_RELEASE_ADVANCE_MASK) >> XGMAC_MTL_FPE_ADVANCE_SHIFT;
#endif
	} else {
		fpe->enabled = 0;
		fpe->pec = 0;
		fpe->afsz = 0;
		fpe->HA_time = 0;
		fpe->RA_time = 0;
	}
	if (copy_to_user(data, fpe, sizeof(*fpe))) {
		ret = -EFAULT;
		goto out_free;
	}
out_free:
		kfree(fpe);
		return ret;
}

static int tc956xmac_ioctl_set_fpe(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956xmac_ioctl_fpe_cfg *fpe;
	int ret = 0;
	unsigned int control = 0;

	fpe = kzalloc(sizeof(*fpe), GFP_KERNEL);
	if (!fpe)
		return -ENOMEM;
	if (copy_from_user(fpe, data, sizeof(*fpe))) {
		ret = -EFAULT;
		goto out_free;
	}
#ifdef TC956X
	if (fpe->enabled) {
		control = readl(priv->ioaddr + XGMAC_MTL_FPE_CTRL_STS);
		control &= ~(XGMAC_MTL_FPE_PEC_MASK | XGMAC_MTL_FPE_AFSZ_MASK);
		control |= ((fpe->pec << XGMAC_MTL_FPE_PEC_SHIFT) & XGMAC_MTL_FPE_PEC_MASK) |
				(fpe->afsz & XGMAC_MTL_FPE_AFSZ_MASK);
		writel(control, priv->ioaddr + XGMAC_MTL_FPE_CTRL_STS);

		control = readl(priv->ioaddr + XGMAC_MTL_FPE_ADVANCE);
		control &= ~(XGMAC_MTL_FPE_HOLD_ADVANCE_MASK | XGMAC_MTL_FPE_RELEASE_ADVANCE_MASK);
		control |= ((fpe->RA_time << XGMAC_MTL_FPE_ADVANCE_SHIFT) & XGMAC_MTL_FPE_RELEASE_ADVANCE_MASK) |
				(fpe->HA_time & XGMAC_MTL_FPE_HOLD_ADVANCE_MASK);
		writel(control, priv->ioaddr + XGMAC_MTL_FPE_ADVANCE);

		control = readl(priv->ioaddr + XGMAC_FPE_CTRL_STS);
		control |= XGMAC_EFPE;
		writel(control, priv->ioaddr + XGMAC_FPE_CTRL_STS);
	} else {
		control = readl(priv->ioaddr + XGMAC_FPE_CTRL_STS);
		control &= ~XGMAC_EFPE;
		writel(control, priv->ioaddr + XGMAC_FPE_CTRL_STS);

		control = readl(priv->ioaddr + XGMAC_MTL_FPE_ADVANCE);
		control &= ~(XGMAC_MTL_FPE_HOLD_ADVANCE_MASK | XGMAC_MTL_FPE_RELEASE_ADVANCE_MASK);
		writel(control, priv->ioaddr + XGMAC_MTL_FPE_ADVANCE);

		control = readl(priv->ioaddr + XGMAC_MTL_FPE_CTRL_STS);
		control &= ~(XGMAC_MTL_FPE_PEC_MASK | XGMAC_MTL_FPE_AFSZ_MASK);
		writel(control, priv->ioaddr + XGMAC_MTL_FPE_CTRL_STS);
	}
#endif

out_free:
	kfree(fpe);
	return ret;
}
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */
static int tc956xmac_ioctl_get_rxp(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956xmac_rx_parser_cfg *cfg = &priv->plat->rxp_cfg;
	struct tc956xmac_ioctl_rxp_cfg *rxp;
	int ret = 0;

	rxp = kzalloc(sizeof(*rxp), GFP_KERNEL);
	if (!rxp)
		return -ENOMEM;

	rxp->enabled = priv->rxp_enabled;
	rxp->frpes = priv->dma_cap.frpes;
	rxp->nve = cfg->nve;
	rxp->npe = cfg->npe;
	memcpy(rxp->entries, cfg->entries, rxp->nve * sizeof(*cfg->entries));
	if (copy_to_user(data, rxp, sizeof(*rxp))) {
		ret = -EFAULT;
		goto out_free;
	}

out_free:
	kfree(rxp);
	return ret;
}

static int tc956xmac_ioctl_set_rxp(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956xmac_rx_parser_cfg *cfg = &priv->plat->rxp_cfg;
	struct tc956xmac_ioctl_rxp_cfg *rxp;
	int ret = 0;

	rxp = kzalloc(sizeof(*rxp), GFP_KERNEL);
	if (!rxp)
		return -ENOMEM;

	if (copy_from_user(rxp, data, sizeof(*rxp))) {
		ret = -EFAULT;
		goto out_free;
	}
	if (rxp->nve > TC956XMAC_RX_PARSER_MAX_ENTRIES) {
		ret = -EINVAL;
		goto out_free;
	}

	if (rxp->enabled) {
		cfg->nve = rxp->nve;
		cfg->npe = rxp->npe;
		memcpy(cfg->entries, rxp->entries,
		       cfg->nve * sizeof(*cfg->entries));
	} else {
		cfg->nve = 0;
		cfg->npe = 0;
		memset(cfg->entries, 0, sizeof(cfg->entries));
	}

	ret = tc956xmac_rx_parser_configuration(priv);
	if (!rxp->enabled)
		ret = 0;

out_free:
	kfree(rxp);
	return ret;
}


static int tc956xmac_ioctl_get_tx_free_desc(struct tc956xmac_priv *priv, void __user *data)
{
	u32 tx_free_desc;
	struct tc956xmac_ioctl_free_desc ioctl_data;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	if ((ioctl_data.queue_idx < priv->plat->tx_queues_to_use) && (ioctl_data.queue_idx != 1) &&
		(priv->plat->tx_dma_ch_owner[ioctl_data.queue_idx] == USE_IN_TC956X_SW)) {

		tx_free_desc = tc956xmac_tx_avail(priv, ioctl_data.queue_idx);
		if (copy_to_user((void __user *)ioctl_data.ptr, &tx_free_desc, sizeof(unsigned int)))
			return -EFAULT;
	} else {
		NMSGPR_ALERT(priv->device, "Channel no %d is invalid\n", ioctl_data.queue_idx);
		return -EFAULT;
	}
	DBGPR_FUNC(priv->device, "<--%s\n", __func__);

	return 0;
}

static int tc956xmac_ioctl_get_connected_speed(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956xmac_ioctl_speed ioctl_data;

	memset(&ioctl_data, 0, sizeof(ioctl_data));

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	ioctl_data.connected_speed = priv->speed;

	if (copy_to_user(data, &ioctl_data, sizeof(ioctl_data)))
		return -EFAULT;

	DBGPR_FUNC(priv->device, "<--%s\n", __func__);
	return 0;
}
#ifdef TC956X_IOCTL_REG_RD_WR_ENABLE

/*!
 * \brief API to read register
 * \param[in] address offset as per tc956x data-sheet
 * \param[in] bar number
 * \return register value
 */
static int tc956xmac_reg_rd(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956xmac_ioctl_reg_rd_wr ioctl_data;
	u32 val;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);
	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

#ifdef TC956X
		if (ioctl_data.bar_num == 4)
			val = readl((void __iomem *)(priv->dev->base_addr + ioctl_data.addr));
		else if (ioctl_data.bar_num == 2)/*SRAM bar number 2*/
			val = readl((void __iomem *)(priv->tc956x_SRAM_pci_base_addr + ioctl_data.addr));
		else if (ioctl_data.bar_num == 0)/*PCI bridge bar number 0*/
			val = readl((void __iomem *)(priv->tc956x_BRIDGE_CFG_pci_base_addr + ioctl_data.addr));
#endif

	if (copy_to_user(ioctl_data.ptr, &val, sizeof(unsigned int)))
		return -EFAULT;
	DBGPR_FUNC(priv->device, "<--%s\n", __func__);

	return 0;

}

/*!
 * \brief API to write register
 * \param[in] address offset as per tc956x data-sheet
 * \return register
 */
static int tc956xmac_reg_wr(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956xmac_ioctl_reg_rd_wr ioctl_data;
	u32 val;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);
	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	if (copy_from_user(&val, ioctl_data.ptr, sizeof(unsigned int)))
		return -EFAULT;

#ifdef TC956X
	if (ioctl_data.bar_num == 4)
		writel(val, (void __iomem *)(priv->dev->base_addr + ioctl_data.addr));
	else if (ioctl_data.bar_num == 2)/*SRAM bar number 2*/
		writel(val, (void __iomem *)(priv->tc956x_SRAM_pci_base_addr + ioctl_data.addr));
	else if (ioctl_data.bar_num == 0)/*PCI bridge bar number 0*/
		writel(val, (void __iomem *)(priv->tc956x_BRIDGE_CFG_pci_base_addr + ioctl_data.addr));
#endif

	DBGPR_FUNC(priv->device, "<--%s\n", __func__);

	return 0;
}
#endif

static int tc956xmac_ioctl_set_mac_loopback(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956xmac_ioctl_loopback ioctl_data;
	u32 value = 0;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	if (priv->mac_loopback_mode == ioctl_data.flags) {
		NDBGPR_L1(priv->device, " MAC loopback mode is already configured for the intended configuration\n");
		return 0;
	}
#ifdef TC956X
	value = readl((void __iomem *)priv->dev->base_addr + XGMAC_RX_CONFIG);
	if (ioctl_data.flags)
		value |= XGMAC_CONFIG_LM;
	else
		value &= ~XGMAC_CONFIG_LM;

	writel(value, (void __iomem *)priv->dev->base_addr + XGMAC_RX_CONFIG);
#endif

	priv->mac_loopback_mode = ioctl_data.flags;

	DBGPR_FUNC(priv->device, "<--%s\n", __func__);
	return 0;
}

static int tc956xmac_ioctl_set_phy_loopback(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956xmac_ioctl_phy_loopback ioctl_data;
#ifdef TC956X
	int ret;
#endif

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);
	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	if (priv->phy_loopback_mode == ioctl_data.flags) {
		NDBGPR_L1(priv->device, " PHY loopback mode is already configured for the intended configuration\n");
		return 0;
	}

	priv->phy_loopback_mode = ioctl_data.flags;

#ifdef TC956X
	if (priv->phy_loopback_mode)
		ret = phy_loopback(priv->dev->phydev, true);
	else
		ret = phy_loopback(priv->dev->phydev, false);

	if (ret)
		return ret;

#endif

	DBGPR_FUNC(priv->device, "<--%s\n", __func__);
	return 0;
}

static int tc956xmac_config_l2_da_filter(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956xmac_ioctl_l2_da_filter ioctl_data;
	int ret = 0;
	unsigned int reg_val = 0;

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	if (ioctl_data.perfect_hash) {
		if (HASH_TABLE_SIZE > 0)
			priv->l2_filtering_mode = 1;
		else
			ret = -1;
	} else
		priv->l2_filtering_mode = 0;

	/* configure the l2 filter matching mode */
	reg_val = readl(priv->ioaddr + XGMAC_PACKET_FILTER);
	reg_val &= ~XGMAC_PACKET_FILTER_DAIF;
	reg_val |= ((ioctl_data.perfect_inverse_match & 0x1)
		    << XGMAC_PACKET_FILTER_DAIF_LPOS);
	writel(reg_val, priv->ioaddr + XGMAC_PACKET_FILTER);
	return ret;
}

static int tc956xmac_config_vlan_filter(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956xmac_ioctl_vlan_filter ioctl_data;
	u32 reg_val;

	if (!(priv->dev->features & NETIF_F_HW_VLAN_CTAG_FILTER))
		return -EPERM;

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	/* Disabling VLAN is not supported */
	if (ioctl_data.filter_enb_dis == 0)
		return -EINVAL;

#ifndef TC956X
	/* configure the vlan filter */
	reg_val = readl(priv->ioaddr + XGMAC_PACKET_FILTER);
	reg_val =  (ioctl_data.filter_enb_dis << XGMAC_VLAN_VLC_SHIFT);
	writel(reg_val, priv->ioaddr + XGMAC_PACKET_FILTER);
#endif
	reg_val = readl(priv->ioaddr + XGMAC_VLAN_TAG);
	reg_val = (reg_val & (~XGMAC_VLANTR_VTIM)) |
		  (ioctl_data.perfect_inverse_match << XGMAC_VLANTR_VTIM_LPOS);
	reg_val = (reg_val & (~XGMAC_VLAN_VTHM)) |
		  (ioctl_data.perfect_hash << XGMAC_VLANTR_VTHM_LPOS);

	/* When VLAN filtering is enabled, then VL/VID
	 * should be > zero, if VLAN is not configured. Otherwise all VLAN
	 * packets will be accepted. Hence we are writting 1 into VL. It also
	 * means that MAC will always receive VLAN pkt with VID = 1 if inverse
	 * march is not set.
	 */
	/* If hash filtering is enabled,
	 * By default enable MAC to calculate vlan hash on only 12-bits of
	 * received VLAN tag (ie only on VLAN id and ignore priority and other
	 * fields)
	 *
	 */

		reg_val = (reg_val & (~XGMAC_VLAN_ETV)) |
			  (1 << XGMAC_VLAN_VLC_SHIFT);

	writel(reg_val, priv->ioaddr + XGMAC_VLAN_TAG);
	priv->vlan_hash_filtering = ioctl_data.perfect_hash;

	KPRINT_INFO("Successfully %s VLAN %s filtering and %s matching\n",
		  (ioctl_data.filter_enb_dis ? "ENABLED" : "DISABLED"),
		  (ioctl_data.perfect_hash ? "HASH" : "PERFECT"),
		  (ioctl_data.perfect_inverse_match ? "INVERSE" : "PERFECT"));

	return 0;
}

/**
 *  tc956xmac_ioctl - Entry point for the Ioctl
 *  @dev: Device pointer.
 *  @rq: An IOCTL specefic structure, that can contain a pointer to
 *  a proprietary structure used to pass information to the driver.
 *  @cmd: IOCTL command
 *  Description:
 *  Currently it supports the phy_mii_ioctl(...) and HW time stamping.
 */
/*!
 * \brief API to Check the FW status
 * \param[in] tc956xmac priv structure
 * \param[in] An IOCTL specefic structure, that can contain a data pointer
 * \return 0 (success) or Error value (fail)
 */
static int tc956x_xgmac_get_fw_status(struct tc956xmac_priv *priv,
					void __user *data)
{
	struct tc956x_ioctl_fwstatus ioctl_data;
	u32 wdt_count1, wdt_count2;
	u32 systick_count1, systick_count2;
	u32 fw_status;

	DBGPR_FUNC(priv->device, "--> %s\n", __func__);

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;


	/* Read the WDT counter and Systik count value */
	wdt_count1 = readl(priv->ioaddr + INTC_INTINTWDMON);
#ifdef TC956X
	systick_count1 = readl(priv->tc956x_SRAM_pci_base_addr
				+ SYSTCIK_SRAM_OFFSET);
#endif

	mdelay(1);

	/* Read the WDT counter and Systick count value */
	wdt_count2 = readl(priv->ioaddr + INTC_INTINTWDMON);
#ifdef TC956X
	systick_count2 = readl(priv->tc956x_SRAM_pci_base_addr
				+ SYSTCIK_SRAM_OFFSET);
#endif
	fw_status  = 1; /* running */

	/*
	 * 1. Firmware running case
	 * wdt_count1 and  wdt_count2  have different values ( second value
	 * read after 1ms)
	 * systick_count1 and, systick_count2  have different vlaue ( second
	 * value read after 1ms)
	 * 2. Firmware not running case
	 * wdt_count1 and  wdt_count2  have same values ( 0 or FULL value)
	 * systick_count1 and, systick_count2 will have same values
	 */
	if ((wdt_count1 == wdt_count2) || (systick_count1 == systick_count2))
		fw_status  = -1; /* Not running */


	ioctl_data.wdt_count = wdt_count2;
	ioctl_data.systick_count = systick_count2;
	ioctl_data.fw_status = fw_status;

	if (copy_to_user(data, &ioctl_data, sizeof(ioctl_data)))
		return -EFAULT;


	DBGPR_FUNC(priv->device, "<-- %s\n", __func__);
	return 0;
}


/**
 * tc956x_ptp_configuration - Configure PTP
 * @priv: driver private structure
 * Description: It is used for configuring the PTP
 * Return value:
 * 0 on success or negative error number.
 */
static void tc956x_ptp_configuration(struct tc956xmac_priv *priv, u32 tcr_config)
{
	struct timespec64 now;
	u32 control, sec_inc;
	u64 temp;

	if (tcr_config == 0) {
		control = PTP_TCR_TSENA | PTP_TCR_TSCTRLSSR
				| PTP_TCR_TSCFUPDT;
		control |= PTP_TCR_PTGE;
		control |= 0x10013e03;
	} else {
		control = tcr_config;
	}

	tc956xmac_config_hw_tstamping(priv, priv->ptpaddr, control);

	/* program Sub Second Increment reg */
#ifdef TC956X
	tc956xmac_config_sub_second_increment(priv,
		priv->ptpaddr, priv->plat->clk_ptp_rate,
		priv->plat->has_xgmac,
		&sec_inc);
#endif

	temp = div_u64(1000000000ULL, sec_inc);

	/*
	 * calculate default added value: formula is :
	 * addend = (2^32)/freq_div_ratio; where, freq_div_ratio = 1e9ns/sec_inc
	 */
	temp = (u64)(temp << 32);

	priv->default_addend = div_u64(temp, TC956X_PTP_SYSCLOCK);

	tc956xmac_config_addend(priv, priv->ptpaddr, priv->default_addend);

	ktime_get_real_ts64(&now);
	tc956xmac_init_systime(priv, priv->ptpaddr, (u32)now.tv_sec, now.tv_nsec);

	control |= PTP_TCR_TSINIT;
	tc956xmac_config_hw_tstamping(priv, priv->ptpaddr, control);

	priv->hwts_tx_en = 1;
	priv->hwts_rx_en = 1;

}

#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
static int pps_configuration(struct tc956xmac_priv *priv,
			 struct tc956xmac_PPS_Config *tc956x_pps_cfg)
{
	unsigned int sec, subsec, val, align_ns = 0;
	int value, interval, width, ppscmd, trgtmodsel = 0x3;

	value = readl(priv->ptpaddr + PTP_TCR);
	if (!(value & 0x00000001)) {
		tc956x_ptp_configuration(priv, 0);
		DBGPR_FUNC(priv->device, "--> %s\n", __func__);
	}


	interval = (tc956x_pps_cfg->ptpclk_freq + tc956x_pps_cfg->ppsout_freq/2)
			/ tc956x_pps_cfg->ppsout_freq;

	width = ((interval * tc956x_pps_cfg->ppsout_duty) + 50)/100 - 1;
	if (width >= interval)
		width = interval - 1;
	if (width < 0)
		width = 0;

	ppscmd = 0x3;	/* cancel start */
	DBGPR_FUNC(priv->device, "interval: %d, width: %d\n", interval, width);

	if (tc956x_pps_cfg->ppsout_align == 1) {
		DBGPR_FUNC(priv->device, "PPS: PPSOut_Config: freq=%dHz, ch=%d, duty=%d, align=%d\n",
			tc956x_pps_cfg->ppsout_freq,
			tc956x_pps_cfg->ppsout_ch,
			tc956x_pps_cfg->ppsout_duty,
			tc956x_pps_cfg->ppsout_align_ns);
	} else {
		DBGPR_FUNC(priv->device, "PPS: PPSOut_Config: freq=%dHz, ch=%d, duty=%d, No alignment\n",
			tc956x_pps_cfg->ppsout_freq,
			tc956x_pps_cfg->ppsout_ch,
			tc956x_pps_cfg->ppsout_duty);
	}

	DBGPR_FUNC(priv->device, ": with PTP Clock freq=%dHz\n", tc956x_pps_cfg->ptpclk_freq);

	if (tc956x_pps_cfg->ppsout_align == 1) {
		align_ns = tc956x_pps_cfg->ppsout_align_ns;
		DBGPR_FUNC(priv->device, "(1000000000/tc956x_pps_cfg->ppsout_freq) : %d, tc956x_pps_cfg->ppsout_align_ns: %d\n",
			(1000000000/tc956x_pps_cfg->ppsout_freq), tc956x_pps_cfg->ppsout_align_ns);
		/* (1000000000/tc956x_pps_cfg->ppsout_freq))  adjust 32ns sync */
		if (align_ns < 32) {
			align_ns += (1000000000 - 32);	/* (1000000000/tc956x_pps_cfg->ppsout_freq)); */
			DBGPR_FUNC(priv->device, "align_ns : %x\n", align_ns);
		} else {
			align_ns -= 32;	/* (1000000000/tc956x_pps_cfg->ppsout_freq) */
		}
	}

#ifdef TC956X
	writel((interval-1), priv->ioaddr + XGMAC_PPSx_INTERVAL(tc956x_pps_cfg->ppsout_ch));
	writel(width, priv->ioaddr + XGMAC_PPSx_WIDTH(tc956x_pps_cfg->ppsout_ch));
#endif

	if (tc956x_pps_cfg->ppsout_ch == 0) {
		val = readl(priv->ioaddr + NFUNCEN4_OFFSET);
		val &= (~0x00000F00);	/* GPIO2 */
		val |= 0x00000100;
		writel(val, priv->ioaddr + NFUNCEN4_OFFSET);
	} else if (tc956x_pps_cfg->ppsout_ch == 1) {
		val = readl(priv->ioaddr + NFUNCEN4_OFFSET);
		val &= (~0x000F0000);	/* GPIO4 */
		val |= 0x00010000;
		writel(val, priv->ioaddr + NFUNCEN4_OFFSET);
	}

#ifdef TC956X
	val = readl(priv->ioaddr + XGMAC_PPS_CONTROL);
	val |= XGMAC_PPSCMDx(tc956x_pps_cfg->ppsout_ch, ppscmd);
	writel(val, priv->ioaddr + XGMAC_PPS_CONTROL);
	val &= ~(0x7 << (tc956x_pps_cfg->ppsout_ch * 8));
	val |= (XGMAC_PPSCMD_STOP << (tc956x_pps_cfg->ppsout_ch * 8)); /* stop pulse train immediately */
	writel(val, priv->ioaddr + XGMAC_PPS_CONTROL);

	sec = readl(priv->ioaddr + PTP_XGMAC_OFFSET + PTP_STSR);		/* PTP seconds */
	subsec = readl(priv->ioaddr + PTP_XGMAC_OFFSET + PTP_STNSR);	/* PTP subseconds */

	/* second roll over */
	if (readl(priv->ioaddr + PTP_XGMAC_OFFSET + PTP_STSR) != sec) {
		sec  = readl(priv->ioaddr + PTP_XGMAC_OFFSET + PTP_STSR);	/* PTP seconds */
		subsec = readl(priv->ioaddr + PTP_XGMAC_OFFSET + PTP_STNSR);/* PTP subseconds */
	}
	DBGPR_FUNC(priv->device, "sec: %x\n", sec);
	DBGPR_FUNC(priv->device, "subsec: %x\n", subsec);
	if (tc956x_pps_cfg->ppsout_align == 1) {
		subsec += PPS_START_DELAY;
		if (subsec >= align_ns) {
			/* s  += 1; */
			DBGPR_FUNC(priv->device, "sec: %x\n", sec);
		}
		sec += 2;
		DBGPR_FUNC(priv->device, "align_ns: %x\n", align_ns);
		/*  PPS target sec */
		writel(sec, priv->ioaddr + XGMAC_PPSx_TARGET_TIME_SEC(tc956x_pps_cfg->ppsout_ch));
		/* PPS target nsec */
		writel(align_ns, priv->ioaddr + XGMAC_PPSx_TARGET_TIME_NSEC(tc956x_pps_cfg->ppsout_ch));
	} else {
		subsec += PPS_START_DELAY;
		if (subsec >= 1000000000) {
			subsec -= 1000000000;
			sec += 1;
		}
		/* set subsecond */
		/* PPS target sec */
		writel(sec, priv->ioaddr + XGMAC_PPSx_TARGET_TIME_SEC(tc956x_pps_cfg->ppsout_ch));
		/* PPS target nsec */
		writel(subsec, priv->ioaddr + XGMAC_PPSx_TARGET_TIME_NSEC(tc956x_pps_cfg->ppsout_ch));
	}

	val = readl(priv->ioaddr + XGMAC_PPS_CONTROL);
	val &= ~GENMASK(((tc956x_pps_cfg->ppsout_ch + 1) * 8) - 1, tc956x_pps_cfg->ppsout_ch * 8);
	val |= (XGMAC_PPSCMD_START << (tc956x_pps_cfg->ppsout_ch * 8)) | 0x10;	/* PPSEN0 */
	val |= XGMAC_TRGTMODSELx(tc956x_pps_cfg->ppsout_ch, trgtmodsel);/* pulse output only */
	writel(val, priv->ioaddr + XGMAC_PPS_CONTROL);

	val = readl(priv->ioaddr + XGMAC_PPS_CONTROL);

#ifdef VERIFY_PPO_USING_AUX
	/* Enable Aux_control (bit 4) */
	val = readl(priv->ioaddr + XGMAC_MAC_AUX_CTRL);
	val |= (0x1 << 4);	/* set bit 4 */
	val |= (0x1 << 0);	/* set bit 0 */
	writel(val, priv->ioaddr + XGMAC_MAC_AUX_CTRL);

	val = readl(priv->ioaddr + NFUNCEN4_OFFSET);
	val |= (0x1 << 4);	/* set bit 4 GPIO1 TRIG00 */
	writel(val, priv->ioaddr + NFUNCEN4_OFFSET);

	/*  enable the trig interrupt */
	val = readl(priv->ioaddr + XGMAC_INT_EN);
	val &= ~(0x1 << 5);	/* clear bit 5 to disable LPI interrupt */
	val |= (0x1 << 12);	/* set bit 12req=50000000Hz */
	writel(val, priv->ioaddr + XGMAC_INT_EN);
#endif
#endif /* TC956X */

	return 0;
}

/*!
 * \brief API to configure pps
 * \param[in] tc956xmac priv structure
 * \param[in] An IOCTL specefic structure, that can contain a data pointer
 * \return 0 (success) or Error value (fail)
 */
static int tc956xmac_set_ppsout(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956xmac_PPS_Config *tc956x_pps_cfg, tc956x_pps_cfg_data;

	DBGPR_FUNC(priv->device, "--> %s\n", __func__);

	if (copy_from_user(&tc956x_pps_cfg_data, data, sizeof(struct tc956xmac_PPS_Config)))
		return -EFAULT;

	tc956x_pps_cfg = (struct tc956xmac_PPS_Config *)(&tc956x_pps_cfg_data);

	if (tc956x_pps_cfg->ppsout_duty <= 0) {
		DBGPR_FUNC(priv->device, "PPS: PPSOut_Config: duty cycle is invalid. Using duty=1\n");
		tc956x_pps_cfg->ppsout_duty = 1;
		return -EFAULT;
	} else if (tc956x_pps_cfg->ppsout_duty >= 100) {
		DBGPR_FUNC(priv->device, "PPS: PPSOut_Config: duty cycle is invalid. Using duty=99\n");
		tc956x_pps_cfg->ppsout_duty = 99;
		return -EFAULT;
	}

	if ((tc956x_pps_cfg->ppsout_ch != 0) && (tc956x_pps_cfg->ppsout_ch != 1)) {
		DBGPR_FUNC(priv->device, "pps channel %d not supported by GPIO\n", tc956x_pps_cfg->ppsout_ch);
		return -EFAULT;
	}

	return(pps_configuration(priv, tc956x_pps_cfg));
}

/*!
 * \brief API to configure PTP clock
 * \param[in] tc956xmac priv structure
 * \param[in] An IOCTL specefic structure, that can contain a data pointer
 * ptp_clock should be > 3.921568 MHz
 * \return 0 (success) or Error value (fail)
 */
static int tc956xmac_ptp_clk_config(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956xmac_PPS_Config *tc956x_pps_cfg, tc956x_pps_cfg_data;
	int ret = 0;
	u32 value = 0;
	__u64 temp = 0;
	__u32 sec_inc;
	struct timespec64 now;

	DBGPR_FUNC(priv->device, "--> %s\n", __func__);

	if (copy_from_user(&tc956x_pps_cfg_data, data, sizeof(struct tc956xmac_PPS_Config))) {
		DBGPR_FUNC(priv->device, "copy_from_user error: ifr data structure\n");
		return -EFAULT;
	}

	tc956x_pps_cfg = (struct tc956xmac_PPS_Config *)(&tc956x_pps_cfg_data);

	/*  Update minimum functional ptpclk_freq */
	if ((tc956x_pps_cfg->ptpclk_freq == 0)
	|| (tc956x_pps_cfg->ptpclk_freq > 250000000)) {
		DBGPR_FUNC(priv->device, "PPS: Invalid PTPCLK_Config: freq=%dHz\n",
			tc956x_pps_cfg->ptpclk_freq);
		return -1;
	}

#ifdef TC956X
	tc956xmac_config_sub_second_increment(priv,
				priv->ptpaddr, tc956x_pps_cfg->ptpclk_freq,
				priv->plat->has_xgmac,
				&sec_inc);
#endif

	DBGPR_FUNC(priv->device, "sec_inc : %x , tc956x_pps_cfg->ptpclk_freq :%d\n", sec_inc, tc956x_pps_cfg->ptpclk_freq);
	temp = div_u64(1000000000ULL, sec_inc);
	temp = (u64)(temp << 32);
	priv->default_addend = div_u64(temp, TC956X_PTP_SYSCLOCK);

	DBGPR_FUNC(priv->device, "priv->default_addend : %x\n", priv->default_addend);
	/*  Add handling of config_addend return */
	tc956xmac_config_addend(priv, priv->ptpaddr, priv->default_addend);

	value = readl(priv->ptpaddr + PTP_TCR);
	/* Note : TSENA never disabled. */
	if (!(value & 0x00000001)) {
		DBGPR_FUNC(priv->device, "tc956x_xgmac_ptp_clk_config : init systime\n");
		/* initialize system time */
		ktime_get_real_ts64(&now);
		tc956xmac_init_systime(priv, priv->ptpaddr, (u32)now.tv_sec, now.tv_nsec);
		value |= PTP_TCR_TSINIT | PTP_TCR_TSENA | PTP_TCR_TSCFUPDT | PTP_TCR_TSCTRLSSR;
		tc956xmac_config_hw_tstamping(priv, priv->ptpaddr, value);
	}

	return ret;
}

/*!
 * \brief API to configure ptp offloading feature
 * \param[in] tc956xmac priv structure
 * \param[in] An IOCTL specefic structure, that can contain a data pointer
 * \return 0 (success) or Error value (fail)
 */
static int tc956xmac_config_ptpoffload(struct tc956xmac_priv *priv, void __user *data)
{
	u32 pto_cntrl;
	u32 varMAC_TCR;
	struct tc956x_config_ptpoffloading ioctl_data;

	DBGPR_FUNC(priv->device, "--> %s\n", __func__);

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	pto_cntrl = XGMAC_PTOEN;	/* enable ptp offloading */

	varMAC_TCR = readl(priv->ptpaddr + PTP_TCR);
	varMAC_TCR &= ~PTP_TCR_MODE_MASK;

	if (ioctl_data.mode == TC956X_PTP_ORDINARY_SLAVE) {
		varMAC_TCR |= PTP_TCR_TSEVNTENA;
		priv->ptp_offloading_mode = TC956X_PTP_ORDINARY_SLAVE;
	} else if (ioctl_data.mode == TC956X_PTP_TRASPARENT_SLAVE) {
		pto_cntrl |= XGMAC_APDREQEN;
		varMAC_TCR |= PTP_TCR_TSEVNTENA;
		varMAC_TCR = varMAC_TCR & (~PTP_GMAC4_TCR_SNAPTYPSEL_1);
		varMAC_TCR |= PTP_TCR_SNAPTYPSEL_1;
		priv->ptp_offloading_mode = TC956X_PTP_TRASPARENT_SLAVE;
	} else if (ioctl_data.mode == TC956X_PTP_ORDINARY_MASTER) {
		pto_cntrl |= XGMAC_ASYNCEN;
		varMAC_TCR |= PTP_TCR_TSEVNTENA;
		varMAC_TCR |= PTP_TCR_TSMSTRENA;
		priv->ptp_offloading_mode = TC956X_PTP_ORDINARY_MASTER;
	} else if (ioctl_data.mode == TC956X_PTP_TRASPARENT_MASTER) {
		pto_cntrl |= XGMAC_ASYNCEN | XGMAC_APDREQEN;
		varMAC_TCR = varMAC_TCR & (~PTP_GMAC4_TCR_SNAPTYPSEL_1);
		varMAC_TCR |= PTP_TCR_SNAPTYPSEL_1;
		varMAC_TCR |= PTP_TCR_TSEVNTENA;
		varMAC_TCR |= PTP_TCR_TSMSTRENA;
		priv->ptp_offloading_mode = TC956X_PTP_TRASPARENT_MASTER;
	} else if (ioctl_data.mode == TC956X_PTP_PEER_TO_PEER_TRANSPARENT) {
		pto_cntrl |= XGMAC_APDREQEN;
		varMAC_TCR |= (3 << PTP_TCR_SNAPTYPSEL_1_LPOS);
		priv->ptp_offloading_mode = TC956X_PTP_PEER_TO_PEER_TRANSPARENT;
	}

	priv->ptp_offload = 1;
	if (ioctl_data.en_dis == TC956X_PTP_OFFLOADING_DISABLE) {
		pto_cntrl = 0;
		varMAC_TCR = readl(priv->ptpaddr + PTP_TCR);
		priv->ptp_offload = 0;
	}

	pto_cntrl |= (ioctl_data.domain_num << 8);
	writel(varMAC_TCR, priv->ptpaddr + PTP_TCR);

	/* Since time registers are already initialized by default, no need to initialize time. */
	if (ioctl_data.mc_uc == 1)
		tc956xmac_set_umac_addr(priv, priv->hw, ioctl_data.mc_uc_addr, 0, PF_DRIVER);

#ifdef TC956X
	writel(pto_cntrl, priv->ioaddr + XGMAC_PTO_CTRL);
#endif
	varMAC_TCR = readl(priv->ptpaddr + PTP_TCR);
	varMAC_TCR &= (~PTP_TCR_TSENMACADDR);
	varMAC_TCR |= ((ioctl_data.mc_uc & 0x1) << PTP_TCR_MC_UC_LPOS);
	writel(varMAC_TCR, priv->ptpaddr + PTP_TCR);

	DBGPR_FUNC(priv->device, "--> %s\n", __func__);

	return 0;
}

/*!
 * \brief API to configure to enable auxiliary timestamp feature
 * \param[in] tc956xmac priv structure
 * \param[in] An IOCTL specefic structure, that can contain a data pointer
 * \return 0 (success) or Error value (fail)
 */
static int tc956xmac_aux_timestamp_enable(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956x_ioctl_aux_snapshot ioctl_data;
	u32 aux_cntrl_en;

	DBGPR_FUNC(priv->device, "--> %s\n", __func__);

#ifdef TC956X
	aux_cntrl_en = readl(priv->ioaddr + XGMAC_MAC_AUX_CTRL);
#endif
	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	if ((ioctl_data.aux_snapshot_ctrl & TC956X_AUX_SNAPSHOT_0))
		aux_cntrl_en |= XGMAC_ATSEN0;
	else
		aux_cntrl_en &= ~XGMAC_ATSEN0;

	if ((ioctl_data.aux_snapshot_ctrl & TC956X_AUX_SNAPSHOT_1))
		aux_cntrl_en |= XGMAC_ATSEN1;
	else
		aux_cntrl_en &= ~XGMAC_ATSEN1;

	if ((ioctl_data.aux_snapshot_ctrl & TC956X_AUX_SNAPSHOT_2))
		aux_cntrl_en |= XGMAC_ATSEN2;
	else
		aux_cntrl_en &= ~XGMAC_ATSEN2;

	if ((ioctl_data.aux_snapshot_ctrl & TC956X_AUX_SNAPSHOT_3))
		aux_cntrl_en |= XGMAC_ATSEN3;
	else
		aux_cntrl_en &= ~XGMAC_ATSEN3;

	/* Auxiliary timestamp FIFO clear */
	aux_cntrl_en |= XGMAC_ATSFC;

#ifdef TC956X
	writel(aux_cntrl_en, priv->ioaddr + XGMAC_MAC_AUX_CTRL);
#endif

	DBGPR_FUNC(priv->device, "--> %s\n", __func__);

	return 0;

}

/*!
 * \brief API to configure to enable/disable onestep timestamp feature
 * \param[in] tc956xmac priv structure
 * \param[in] An IOCTL specefic structure, that can contain a data pointer
 * \return 0 (success) or Error value (fail)
 */
static int tc956xmac_config_onestep_timestamp(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956x_config_ost ioctl_data;

	DBGPR_FUNC(priv->device, "--> %s\n", __func__);

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	if ((priv->dma_cap.osten) && (ioctl_data.en_dis == 1))
		priv->ost_en = 1;
	else
		priv->ost_en = 0;

	return 0;

}
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */

static int tc956xmac_sa_vlan_ins_config(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956xmac_ioctl_sa_ins_cfg ioctl_data;
	u32 reg_data;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);
	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	if (priv->dma_cap.vlins) {
		switch (ioctl_data.cmd) {
		case TC956XMAC_SA0_VLAN_INS_REP_REG:
			priv->sa_vlan_ins_via_reg = ioctl_data.control_flag;
			if (ioctl_data.control_flag == TC956XMAC_SA0_NONE)
				memcpy(priv->ins_mac_addr, priv->dev->dev_addr, ETH_ALEN);
			else
				memcpy(priv->ins_mac_addr, ioctl_data.mac_addr, ETH_ALEN);

#ifdef TC956X
			tc956xmac_set_umac_addr(priv, priv->hw, priv->ins_mac_addr, 0, PF_DRIVER);
			reg_data = readl(priv->ioaddr + XGMAC_TX_CONFIG);
			reg_data = (reg_data & (~XGMAC_CONFIG_SARC)) |
				   (priv->sa_vlan_ins_via_reg << XGMAC_CONFIG_SARC_SHIFT);
			writel(reg_data, priv->ioaddr + XGMAC_TX_CONFIG);
#endif

			NMSGPR_ALERT(priv->device, "SA will use MAC0 with register for configuration %d\n", priv->sa_vlan_ins_via_reg);
			break;
		case TC956XMAC_SA1_VLAN_INS_REP_REG:
			priv->sa_vlan_ins_via_reg = ioctl_data.control_flag;
			if (ioctl_data.control_flag == TC956XMAC_SA1_NONE)
				memcpy(priv->ins_mac_addr, priv->dev->dev_addr, ETH_ALEN);
			else
				memcpy(priv->ins_mac_addr, ioctl_data.mac_addr, ETH_ALEN);

#ifdef TC956X
			tc956xmac_set_umac_addr(priv, priv->hw, priv->ins_mac_addr, 1, PF_DRIVER);
			reg_data = readl(priv->ioaddr + XGMAC_TX_CONFIG);
			reg_data = (reg_data & (~XGMAC_CONFIG_SARC)) |
				   (priv->sa_vlan_ins_via_reg << XGMAC_CONFIG_SARC_SHIFT);
			writel(reg_data, priv->ioaddr + XGMAC_TX_CONFIG);
#endif
			NMSGPR_ALERT(priv->device, "SA will use MAC1 with register for configuration %d\n", priv->sa_vlan_ins_via_reg);
			break;
		default:
			return -EINVAL;
		}
	} else {
		NMSGPR_ALERT(priv->device, "Device doesn't supports SA Insertion/Replacement\n");
		return -EINVAL;
	}

	DBGPR_FUNC(priv->device, "<--%s\n", __func__);
	return 0;
}

static int tc956xmac_get_tx_qcnt(struct tc956xmac_priv *priv, void __user *data)
{
	u32 tx_qcnt = 0;
	struct tc956xmac_ioctl_tx_qcnt ioctl_data;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	tx_qcnt = priv->plat->tx_queues_to_use;
	if (copy_to_user((void __user *)ioctl_data.ptr, &tx_qcnt, sizeof(unsigned int)))
		return -EFAULT;

	DBGPR_FUNC(priv->device, "<--%s\n", __func__);

	return 0;
}

static int tc956xmac_get_rx_qcnt(struct tc956xmac_priv *priv, void __user *data)
{
	u32 rx_qcnt = 0;
	struct tc956xmac_ioctl_rx_qcnt ioctl_data;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	rx_qcnt = priv->plat->rx_queues_to_use;
	if (copy_to_user((void __user *)ioctl_data.ptr, &rx_qcnt, sizeof(unsigned int)))
		return -EFAULT;

	DBGPR_FUNC(priv->device, "<--%s\n", __func__);

	return 0;
}

/*!
 * \brief API to read pcie conf register
 * \param[in] address offset as per tc956x data-sheet
 * \return -EFAULT in case of error, otherwise 0
 * \description register can be read for pcie conf between 0 to 0xFFF.
 */
static int tc956xmac_pcie_config_reg_rd(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956xmac_ioctl_pcie_reg_rd_wr ioctl_data;
	u32 val;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	pci_read_config_dword(priv->plat->pdev, ioctl_data.addr, &val);

	if (copy_to_user((void __user *)ioctl_data.ptr, &val, sizeof(unsigned int)))
		return -EFAULT;

	DBGPR_FUNC(priv->device, "<--%s\n", __func__);

	return 0;
}

/*!
 * \brief API to write pcie conf register
 * \param[in] address offset as per tc956x data-sheet
 * \return -EFAULT in case of error, otherwise 0
 * \description register can be written for pcie conf between 0 to 0xFFF.
 */
static int tc956xmac_pcie_config_reg_wr(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956xmac_ioctl_pcie_reg_rd_wr ioctl_data;
	u32 val;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	if (copy_from_user(&val, (const void __user *) ioctl_data.ptr, sizeof(unsigned int)))
		return -EFAULT;

	pci_write_config_dword(priv->plat->pdev, ioctl_data.addr, val);

	DBGPR_FUNC(priv->device, "<--%s\n", __func__);

	return 0;
}

#ifndef TC956X
/*!
 * \brief IOCTL to enable/disable VLAN Rx Stripping
 * \param[in] priv driver private structure
 * \param[in] data user data
 * \return -EFAULT in case of error, otherwise 0
 * \description enable/disable stripping.
 * Enable this function only if NETIF_F_HW_VLAN_CTAG_RX is user-configureable.
 */
static int tc956xmac_vlan_strip_config(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956xmac_ioctl_vlan_strip_cfg ioctl_data;
	u32 reg_data;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);
	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	if (ioctl_data.enabled == 1) {
		/* Enable VLAN Stripping */
#ifdef TC956X
		reg_data = readl(priv->ioaddr + XGMAC_VLAN_TAG);
		reg_data &= ~XGMAC_VLAN_EVLS;
		reg_data |= 0x3 << XGMAC_VLAN_EVLS_SHIFT;

		writel(reg_data, priv->ioaddr + XGMAC_VLAN_TAG);
#endif
	} else {
		/* Disable VLAN Stripping */
#ifdef TC956X
		reg_data = readl(priv->ioaddr + XGMAC_VLAN_TAG);
		reg_data &= ~XGMAC_VLAN_EVLS;

		writel(reg_data, priv->ioaddr + XGMAC_VLAN_TAG);
#endif
	}
	return 0;
}
#endif

#ifdef TC956X
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
static int tc956xmac_mode1_usp_lane_change_4_to_1(struct tc956xmac_priv *priv, void __iomem *ioaddr)
{
	u32 reg_data;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);


	/* Clear SW_USP_TL_PM_BWCHANGE Register in Glue Logic Register rsvd_rw0 */
	writel(0, ioaddr + TC956X_GLUE_RSVD_RW0);

	/* Set SW_USP_TL_PM_BWCHANGE Register in Glue Logic Register rsvd_rw0 to
	 * direct Link Width Change (x4 --> x1) to PCIe Switch USP
	 */
	writel(USP_LINK_WIDTH_CHANGE_4_TO_1, ioaddr + TC956X_GLUE_RSVD_RW0);

	/* Set phy_core_reg_access_enable Register in Glue Logic Register to select
	 * PHY_CORE 0 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
	reg_data &= ~PHY_CORE_ENABLE_MASK;
	reg_data |= (PHY_CORE_0_ENABLE & PHY_CORE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);


	/* Set lane_access_enable Register in PHY PHY_CORE 0 PCS Global Register to
	 * select Lane 1 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
	reg_data &= ~LANE_ENABLE_MASK;
	reg_data |= (LANE_1_ENABLE & LANE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);


	/*Set pcs2pma_power_ctrl Register in PHY PHY_CORE0 Lane 1 PMA Lane Register
	 * to select Lane 1 to set Low Power mode
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_POWER_CTL);
	reg_data &= ~POWER_CTL_MASK;
	reg_data |= (POWER_CTL_LOW_POWER_ENABLE & POWER_CTL_MASK);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_POWER_CTL);


	/* Set pcs2pma_power_ctrl_ovren Register in PHY PHY_CORE 0 Lane 1 PMA Lane Register
	 * to select Lane 1 to set Low Power mode
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_OVEREN_POWER_CTL);
	writel((reg_data | POWER_CTL_OVER_ENABLE), ioaddr + TC956X_PHY_CORE0_OVEREN_POWER_CTL);


	/* Set phy_core_reg_access_enable Register in Glue Logic Register to
	 * select PHY_CORE 1 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
	reg_data &= ~PHY_CORE_ENABLE_MASK;
	reg_data |= (PHY_CORE_1_ENABLE & PHY_CORE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

	/*Set lane_access_enable Register in PHY PHY_CORE 1 PCS Global Register to
	 * select Lane 2/3 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_GL_LANE_ACCESS);
	reg_data &= ~LANE_ENABLE_MASK;
	reg_data |= ((LANE_1_ENABLE | LANE_0_ENABLE) & LANE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_GL_LANE_ACCESS);

	/*Set pcs2pma_power_ctrl Register in PHY PHY_CORE 1 Lane 2/3 PMA Lane Register
	 * to select Lane 2/3 to set Low Power mode
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_POWER_CTL);
	reg_data &= ~POWER_CTL_MASK;
	reg_data |= (POWER_CTL_LOW_POWER_ENABLE & POWER_CTL_MASK);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_POWER_CTL);

	/* Set pcs2pma_power_ctrl_ovren Register in PHY PHY_CORE 1 Lane 2/3 PMA Lane Register
	 * to select Lane 2/3 to set Low Power mode
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_OVEREN_POWER_CTL);
	writel((reg_data | POWER_CTL_OVER_ENABLE), ioaddr + TC956X_PHY_CORE1_OVEREN_POWER_CTL);

	return 0;
}


static int tc956xmac_mode1_usp_lane_change_4_to_2(struct tc956xmac_priv *priv, void __iomem *ioaddr)
{
	u32 reg_data;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	/* Clear SW_USP_TL_PM_BWCHANGE Register in Glue Logic Register rsvd_rw0 */
	writel(0, ioaddr + TC956X_GLUE_RSVD_RW0);

	/* Set SW_USP_TL_PM_BWCHANGE Register in Glue Logic Register rsvd_rw0 to
	 * direct Link Width Change (x4 --> x2) to PCIe Switch USP
	 */
	writel(USP_LINK_WIDTH_CHANGE_4_TO_2, ioaddr + TC956X_GLUE_RSVD_RW0);


	/* Set phy_core_reg_access_enable Register in Glue Logic Register to
	 * select PHY_CORE 1 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
	reg_data &= ~PHY_CORE_ENABLE_MASK;
	reg_data |= (PHY_CORE_1_ENABLE & PHY_CORE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

	/*Set lane_access_enable Register in PHY PHY_CORE 1 PCS Global Register to
	 * select Lane 2/3 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_GL_LANE_ACCESS);
	reg_data &= ~LANE_ENABLE_MASK;
	reg_data |= ((LANE_1_ENABLE | LANE_0_ENABLE) & LANE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_GL_LANE_ACCESS);

	/*Set pcs2pma_power_ctrl Register in PHY PHY_CORE 1 Lane 2/3 PMA Lane Register to
	 * select Lane 2/3 to set Low Power mode
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_POWER_CTL);
	reg_data &= ~POWER_CTL_MASK;
	reg_data |= (POWER_CTL_LOW_POWER_ENABLE & POWER_CTL_MASK);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_POWER_CTL);

	/* Set pcs2pma_power_ctrl_ovren Register in PHY PHY_CORE 1 Lane 2/3 PMA Lane Register to
	 * select Lane 2/3 to set Low Power mode
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_OVEREN_POWER_CTL);
	writel((reg_data | POWER_CTL_OVER_ENABLE), ioaddr + TC956X_PHY_CORE1_OVEREN_POWER_CTL);

	return 0;
}

static int tc956xmac_mode1_usp_lane_change_1_2_to_4(struct tc956xmac_priv *priv, void __iomem *ioaddr)
{
	u32 reg_data;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);


	/* Set phy_core_reg_access_enable Register in Glue Logic Register to
	 * select PHY_CORE 1 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
	reg_data &= ~PHY_CORE_ENABLE_MASK;
	reg_data |= (PHY_CORE_1_ENABLE & PHY_CORE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

	/*Set lane_access_enable Register in PHY PHY_CORE 1 PCS Global Register to
	 * select Lane 2/3 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_GL_LANE_ACCESS);
	reg_data &= ~LANE_ENABLE_MASK;
	reg_data |= ((LANE_1_ENABLE | LANE_0_ENABLE) & LANE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_GL_LANE_ACCESS);

	/* Clear pcs2pma_power_ctrl_ovren Register in PHY PHY_CORE 1 Lane 2/3 PMA Lane Register to
	 * select Lane 2/3 to set Low Power mode
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_OVEREN_POWER_CTL);
	reg_data &= (~POWER_CTL_OVER_ENABLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_OVEREN_POWER_CTL);

	/* Clear Rate 0 pc_debug_p1_toggle Register in PHY PHY_CORE 1 Lane 2/3 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_MISC_RW0_R0);
	reg_data &= (~PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_MISC_RW0_R0);

	/*Clear Rate 1 pc_debug_p1_toggle Register in PHY PHY_CORE 1 Lane 2/3 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_MISC_RW0_R1);
	reg_data &= (~PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_MISC_RW0_R1);

	/*Clear Rate 2 pc_debug_p1_toggle Register in PHY PHY_CORE 1 Lane 2/3 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_MISC_RW0_R2);
	reg_data &= (~PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_MISC_RW0_R2);

	/* Set Rate 0 pc_debug_p1_toggle Register in PHY PHY_CORE 1 Lane 2/3 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_MISC_RW0_R0);
	reg_data |= (PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_MISC_RW0_R0);

	/*Set Rate 1 pc_debug_p1_toggle Register in PHY PHY_CORE 1 Lane 2/3 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_MISC_RW0_R1);
	reg_data |= (PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_MISC_RW0_R1);

	/*Set Rate 2 pc_debug_p1_toggle Register in PHY PHY_CORE 1 Lane 2/3 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_MISC_RW0_R2);
	reg_data |= (PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_MISC_RW0_R2);


	/* Set phy_core_reg_access_enable Register in Glue Logic Register to
	 * select PHY_CORE 0 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
	reg_data &= ~PHY_CORE_ENABLE_MASK;
	reg_data |= (PHY_CORE_0_ENABLE & PHY_CORE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

	/* Set lane_access_enable Register in PHY PHY_CORE 0 PCS Global Register to
	 * select Lane 0/1 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
	reg_data &= ~LANE_ENABLE_MASK;
	reg_data |= ((LANE_1_ENABLE | LANE_0_ENABLE) & LANE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

	/* Clear pcs2pma_power_ctrl_ovren Register in PHY PHY_CORE 0 Lane 0/1 PMA Lane Register to
	 * select Lane 0/1 to set Low Power mode
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_OVEREN_POWER_CTL);
	reg_data &= (~POWER_CTL_OVER_ENABLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_OVEREN_POWER_CTL);

	/* Clear Rate 0 pc_debug_p1_toggle Register in PHY PHY_CORE 0 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_MISC_RW0_R0);
	reg_data &= (~PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_MISC_RW0_R0);

	/*Clear Rate 1 pc_debug_p1_toggle Register in PHY PHY_CORE 0 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_MISC_RW0_R1);
	reg_data &= (~PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_MISC_RW0_R1);

	/*Clear Rate 2 pc_debug_p1_toggle Register in PHY PHY_CORE 0 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_MISC_RW0_R2);
	reg_data &= (~PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_MISC_RW0_R2);

	/* Set Rate 0 pc_debug_p1_toggle Register in PHY PHY_CORE 0 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_MISC_RW0_R0);
	reg_data |= (PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_MISC_RW0_R0);

	/* Wait for 100us */
	udelay(100);

	/*Set Rate 1 pc_debug_p1_toggle Register in PHY PHY_CORE 0 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_MISC_RW0_R1);
	reg_data |= (PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_MISC_RW0_R1);

	/* Wait for 100us */
	udelay(100);

	/*Set Rate 2 pc_debug_p1_toggle Register in PHY PHY_CORE 0 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_MISC_RW0_R2);
	reg_data |= (PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_MISC_RW0_R2);

	/* Wait for 100us */
	udelay(100);

	/* Clear SW_USP_TL_PM_BWCHANGE Register in Glue Logic Register rsvd_rw0 */
	writel(0, ioaddr + TC956X_GLUE_RSVD_RW0);

	/* Set SW_USP_TL_PM_BWCHANGE Register in Glue Logic Register rsvd_rw0 to
	 * direct Link Width Change (x1/x2 --> x4) to PCIe Switch USP
	 */
	writel(USP_LINK_WIDTH_CHANGE_1_2_TO_4, ioaddr + TC956X_GLUE_RSVD_RW0);


	return 0;
}
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */

static int tc956xmac_mode2_usp_lane_change_2_to_1(struct tc956xmac_priv *priv, void __iomem *ioaddr)
{
	u32 reg_data;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	/* Clear SW_USP_TL_PM_BWCHANGE Register in Glue Logic Register rsvd_rw0 */
	writel(0, ioaddr + TC956X_GLUE_RSVD_RW0);

	/* Set SW_USP_TL_PM_BWCHANGE Register in Glue Logic Register rsvd_rw0 to
	 * direct Link Width Change (x2 --> x1) to PCIe Switch USP
	 */
	writel(USP_LINK_WIDTH_CHANGE_2_TO_1, ioaddr + TC956X_GLUE_RSVD_RW0);


	/* Set phy_core_reg_access_enable Register in Glue Logic Register to
	 * select PHY_CORE 0 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
	reg_data &= ~PHY_CORE_ENABLE_MASK;
	reg_data |= (PHY_CORE_0_ENABLE & PHY_CORE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

	/*Set lane_access_enable Register in PHY PHY_CORE 0 PCS Global Register to
	 * select Lane 1 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
	reg_data &= ~LANE_ENABLE_MASK;
	reg_data |= (LANE_1_ENABLE & LANE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

	/*Set pcs2pma_power_ctrl Register in PHY PHY_CORE0 Lane 1 PMA Lane Register to
	 * select Lane 1 to set Low Power mode
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_POWER_CTL);
	reg_data &= ~POWER_CTL_MASK;
	reg_data |= (POWER_CTL_LOW_POWER_ENABLE & POWER_CTL_MASK);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_POWER_CTL);

	/* Set pcs2pma_power_ctrl_ovren Register in PHY PHY_CORE 0 Lane 1 PMA Lane Register to
	 * select Lane 1 to set Low Power mode
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_OVEREN_POWER_CTL);
	writel((reg_data | POWER_CTL_OVER_ENABLE), ioaddr + TC956X_PHY_CORE0_OVEREN_POWER_CTL);

	return 0;
}

static int tc956xmac_mode2_usp_lane_change_1_to_2(struct tc956xmac_priv *priv, void __iomem *ioaddr)
{
	u32 reg_data;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);


	/* Set phy_core_reg_access_enable Register in Glue Logic Register to
	 * select PHY_CORE 0 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
	reg_data &= ~PHY_CORE_ENABLE_MASK;
	reg_data |= (PHY_CORE_0_ENABLE & PHY_CORE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

	/* Set lane_access_enable Register in PHY PHY_CORE 0 PCS Global Register to
	 * select Lane 0/1 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
	reg_data &= ~LANE_ENABLE_MASK;
	reg_data |= ((LANE_1_ENABLE | LANE_0_ENABLE) & LANE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

	/* Clear pcs2pma_power_ctrl_ovren Register in PHY PHY_CORE 0 Lane 0/1 PMA Lane Register to
	 * select Lane 0/1 to set Low Power mode
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_OVEREN_POWER_CTL);
	reg_data &= (~POWER_CTL_OVER_ENABLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_OVEREN_POWER_CTL);

	/* Clear Rate 0 pc_debug_p1_toggle Register in PHY PHY_CORE 0 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_MISC_RW0_R0);
	reg_data &= (~PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_MISC_RW0_R0);

	/*Clear Rate 1 pc_debug_p1_toggle Register in PHY PHY_CORE 0 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_MISC_RW0_R1);
	reg_data &= (~PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_MISC_RW0_R1);

	/*Clear Rate 2 pc_debug_p1_toggle Register in PHY PHY_CORE 0 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_MISC_RW0_R2);
	reg_data &= (~PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_MISC_RW0_R2);

	/* Set Rate 0 pc_debug_p1_toggle Register in PHY PHY_CORE 0 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_MISC_RW0_R0);
	reg_data |= (PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_MISC_RW0_R0);

	/* Wait for 100us */
	udelay(100);

	/*Set Rate 1 pc_debug_p1_toggle Register in PHY PHY_CORE 0 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_MISC_RW0_R1);
	reg_data |= (PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_MISC_RW0_R1);

	/* Wait for 100us */
	udelay(100);

	/*Set Rate 2 pc_debug_p1_toggle Register in PHY PHY_CORE 0 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_MISC_RW0_R2);
	reg_data |= (PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_MISC_RW0_R2);

	/* Wait for 100us */
	udelay(100);

	/* Clear SW_USP_TL_PM_BWCHANGE Register in Glue Logic Register rsvd_rw0 */
	writel(0, ioaddr + TC956X_GLUE_RSVD_RW0);

	/* Set SW_USP_TL_PM_BWCHANGE Register in Glue Logic Register rsvd_rw0 to
	 * direct Link Width Change (x1 --> x2) to PCIe Switch USP
	 */
	writel(USP_LINK_WIDTH_CHANGE_1_TO_2, ioaddr + TC956X_GLUE_RSVD_RW0);


	return 0;
}

#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
static int tc956xmac_mode2_dsp1_lane_change_2_to_1(struct tc956xmac_priv *priv, void __iomem *ioaddr)
{
	u32 reg_data;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	/* Clear SW_DSP1_TL_PM_BWCHANGE Register in Glue Logic Register rsvd_rw0 */
	writel(0, ioaddr + TC956X_GLUE_RSVD_RW0);

	/* Set SW_DSP1_TL_PM_BWCHANGE Register in Glue Logic Register rsvd_rw0 to
	 * direct Link Width Change (x2 --> x1) to PCIe Switch DSP1
	 */
	writel(DSP1_LINK_WIDTH_CHANGE_2_TO_1, ioaddr + TC956X_GLUE_RSVD_RW0);

	/* Set phy_core_reg_access_enable Register in Glue Logic Register to
	 * select PHY_CORE 1 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
	reg_data &= ~PHY_CORE_ENABLE_MASK;
	reg_data |= (PHY_CORE_1_ENABLE & PHY_CORE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

	/*Set lane_access_enable Register in PHY PHY_CORE 1 PCS Global Register to
	 * select Lane 1 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_GL_LANE_ACCESS);
	reg_data &= ~LANE_ENABLE_MASK;
	reg_data |= (LANE_1_ENABLE & LANE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_GL_LANE_ACCESS);

	/*Set pcs2pma_power_ctrl Register in PHY PHY_CORE 1 Lane 1 PMA Lane Register to
	 * select Lane 1 to set Low Power mode
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_POWER_CTL);
	reg_data &= ~POWER_CTL_MASK;
	reg_data |= (POWER_CTL_LOW_POWER_ENABLE & POWER_CTL_MASK);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_POWER_CTL);

	/* Set pcs2pma_power_ctrl_ovren Register in PHY PHY_CORE 1 Lane 1 PMA Lane Register to
	 * select Lane 1 to set Low Power mode
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_OVEREN_POWER_CTL);
	writel((reg_data | POWER_CTL_OVER_ENABLE), ioaddr + TC956X_PHY_CORE1_OVEREN_POWER_CTL);

	return 0;
}

static int tc956xmac_mode2_dsp1_lane_change_1_to_2(struct tc956xmac_priv *priv, void __iomem *ioaddr)
{
	u32 reg_data;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);


	/* Set phy_core_reg_access_enable Register in Glue Logic Register to
	 * select PHY_CORE 1 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
	reg_data &= ~PHY_CORE_ENABLE_MASK;
	reg_data |= (PHY_CORE_1_ENABLE & PHY_CORE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);


	/* Set lane_access_enable Register in PHY PHY_CORE 1 PCS Global Register to
	 * select Lane 0/1 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_GL_LANE_ACCESS);
	reg_data &= ~LANE_ENABLE_MASK;
	reg_data |= ((LANE_1_ENABLE | LANE_0_ENABLE) & LANE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_GL_LANE_ACCESS);

	/* Clear pcs2pma_power_ctrl_ovren Register in PHY PHY_CORE 1 Lane 0/1 PMA Lane Register to
	 * select Lane 0/1 to set Low Power mode
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_OVEREN_POWER_CTL);
	reg_data &= (~POWER_CTL_OVER_ENABLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_OVEREN_POWER_CTL);

	/* Clear Rate 0 pc_debug_p1_toggle Register in PHY PHY_CORE 1 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_MISC_RW0_R0);
	reg_data &= (~PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_MISC_RW0_R0);

	/*Clear Rate 1 pc_debug_p1_toggle Register in PHY PHY_CORE 1 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_MISC_RW0_R1);
	reg_data &= (~PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_MISC_RW0_R1);

	/*Clear Rate 2 pc_debug_p1_toggle Register in PHY PHY_CORE 1 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_MISC_RW0_R2);
	reg_data &= (~PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_MISC_RW0_R2);

	/* Set Rate 0 pc_debug_p1_toggle Register in PHY PHY_CORE 1 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_MISC_RW0_R0);
	reg_data |= (PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_MISC_RW0_R0);

	/* Wait for 100us */
	udelay(100);

	/*Set Rate 1 pc_debug_p1_toggle Register in PHY PHY_CORE 1 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_MISC_RW0_R1);
	reg_data |= (PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_MISC_RW0_R1);

	/* Wait for 100us */
	udelay(100);

	/*Set Rate 2 pc_debug_p1_toggle Register in PHY PHY_CORE 1 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_MISC_RW0_R2);
	reg_data |= (PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_MISC_RW0_R2);

	/* Wait for 100us */
	udelay(100);

	/* Clear SW_DSP1_TL_PM_BWCHANGE Register in Glue Logic Register rsvd_rw0 */
	writel(0, ioaddr + TC956X_GLUE_RSVD_RW0);

	/* Set SW_DSP1_TL_PM_BWCHANGE Register in Glue Logic Register rsvd_rw0 to
	 * direct Link Width Change (x1 --> x2) to PCIe Switch DSP1
	 */
	writel(DSP1_LINK_WIDTH_CHANGE_1_TO_2, ioaddr + TC956X_GLUE_RSVD_RW0);


	return 0;
}
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */

static int tc956xmac_pcie_lane_change(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956x_ioctl_pcie_lane_change ioctl_data;
	u32 reg_data;
	u32 pcie_mode;
	enum lane_width usp_curr_lane_width, dsp1_curr_lane_width;
	void __iomem *ioaddr;

	ioaddr = priv->ioaddr;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	/* Return error if the interface where lane change request sent is not Port 0 */
	if (priv->port_num != RM_PF0_ID) {
		KPRINT_INFO("Lane change requested through wrong interface\n\r");
		return -EINVAL;
	}

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	/* Read mode setting register
	 * Mode settings values 0:Setting A: x4x1x1, 1:Setting B: x2x2x1
	 */
	reg_data = readl(priv->ioaddr + NMODESTS_OFFSET);
	pcie_mode = (reg_data & NMODESTS_MODE2) >> NMODESTS_MODE2_SHIFT;

	KPRINT_INFO("Pcie mode: %d\n\r", pcie_mode);

	/* Get the current lane setting from glue lane monitor register */
	reg_data = readl(priv->ioaddr + TC956X_GLUE_TL_NUM_LANES_MON);
	usp_curr_lane_width = (reg_data & USP_LANE_WIDTH_MASK);
	dsp1_curr_lane_width = ((reg_data & DSP1_LANE_WIDTH_MASK) >> DSP1_LANE_WIDTH_SHIFT);

	switch (pcie_mode) {
	case TC956X_PCIE_SETTING_A: /* 0:Setting A: x4x1x1 mode */
		/* Only USP port lane can be changed in this setting, others return error */
		if (ioctl_data.port != UPSTREAM_PORT) {
			KPRINT_INFO("Lane change requested for unsupported port\n\r");
			return -EINVAL;
		}

		/* If target link width is same as current width, return 0*/
		if (usp_curr_lane_width == ioctl_data.target_lane_width) {
			KPRINT_INFO("Already port is in requested lane. No change required\n\r");
			return 0;
		}
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
		/* All lane width change from x4 to x1, x4 to x2, x1/x2 to x4 are allowed */
		if (usp_curr_lane_width == LANE_4 && ioctl_data.target_lane_width == LANE_1)
			return tc956xmac_mode1_usp_lane_change_4_to_1(priv, ioaddr);
		else if (usp_curr_lane_width == LANE_4 && ioctl_data.target_lane_width == LANE_2)
			return tc956xmac_mode1_usp_lane_change_4_to_2(priv, ioaddr);
		else if (ioctl_data.target_lane_width == LANE_4) /* x1/x2 to x4*/
			return tc956xmac_mode1_usp_lane_change_1_2_to_4(priv, ioaddr);
		else
			return -EINVAL;
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */

	break;
	case TC956X_PCIE_SETTING_B: /* 1:Setting B: x2x2x1 mode */
		/* Only USP and DSP1 port lane can be changed in this setting, others return error */
		if ((ioctl_data.port != UPSTREAM_PORT) &&
			(ioctl_data.port != DOWNSTREAM_PORT1)) {
			KPRINT_INFO("Lane change requested for unsupported port\n\r");
			return -EINVAL;
		}

		/* If target link width is same as current width, return 0*/
		if (ioctl_data.port == UPSTREAM_PORT) {
			if (usp_curr_lane_width == ioctl_data.target_lane_width) {
				KPRINT_INFO("Already port is in requested lane. No change required\n\r");
				return 0;
			}

			if (usp_curr_lane_width == LANE_2 && ioctl_data.target_lane_width == LANE_1)
				return tc956xmac_mode2_usp_lane_change_2_to_1(priv, ioaddr);
			else if (usp_curr_lane_width == LANE_1 && ioctl_data.target_lane_width == LANE_2)
				return tc956xmac_mode2_usp_lane_change_1_to_2(priv, ioaddr);
			else
				return -EINVAL;
		}

		if (ioctl_data.port == DOWNSTREAM_PORT1) {
			if (dsp1_curr_lane_width == ioctl_data.target_lane_width)
				return 0;
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
			if (dsp1_curr_lane_width == LANE_2 && ioctl_data.target_lane_width == LANE_1)
				return tc956xmac_mode2_dsp1_lane_change_2_to_1(priv, ioaddr);
			else if (dsp1_curr_lane_width == LANE_1 && ioctl_data.target_lane_width == LANE_2)
				return tc956xmac_mode2_dsp1_lane_change_1_to_2(priv, ioaddr);
			else
				return -EINVAL;
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */
		}

	break;

	default:
	break;

	}

	return 0;
}


static int tc956xmac_pcie_set_tx_margin(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956x_ioctl_pcie_set_tx_margin ioctl_data;
	u32 reg_data;
	u32 pcie_mode;
	void __iomem *ioaddr;

	ioaddr = priv->ioaddr;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	/* Return error if the interface where lane change request sent is not Port 0 */
	if (priv->port_num != RM_PF0_ID) {
		KPRINT_INFO("Tx Margin change requested through wrong interface\n\r");
		return -EINVAL;
	}

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	/* Read mode setting register
	 * Mode settings values 0:Setting A: x4x1x1, 1:Setting B: x2x2x1
	 */
	reg_data = readl(priv->ioaddr + NMODESTS_OFFSET);
	pcie_mode = (reg_data & NMODESTS_MODE2) >> NMODESTS_MODE2_SHIFT;

	KPRINT_INFO("Pcie mode: %d\n\r", pcie_mode);

	switch (pcie_mode) {
	case TC956X_PCIE_SETTING_A: /* 0:Setting A: x4x1x1 mode */
		switch (ioctl_data.port) {
		case UPSTREAM_PORT:
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= (PHY_CORE_0_ENABLE | PHY_CORE_1_ENABLE);
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			/* Set lane_access_enable Register in PHY PHY_CORE 0 PCS Global Register to
			 * select Lane 0/1 to access by APB I/F
			 */
			reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
			reg_data &= ~LANE_ENABLE_MASK;
			reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
			writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
		break;
		case DOWNSTREAM_PORT1:
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_2_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
		break;
		case DOWNSTREAM_PORT2:
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_3_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
		break;
		default:
			return -EINVAL;
		}

	break;
	case TC956X_PCIE_SETTING_B: /* 1:Setting B: x2x2x1 mode */
		switch (ioctl_data.port) {
		case UPSTREAM_PORT:
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_0_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			/* Set lane_access_enable Register in PHY PHY_CORE 0 PCS Global Register to
			 * select Lane 0/1 to access by APB I/F
			 */
			reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
			reg_data &= ~LANE_ENABLE_MASK;
			reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
			writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
		break;
		case DOWNSTREAM_PORT1:
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_1_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			/* Set lane_access_enable Register in PHY PHY_CORE 0 PCS Global Register to
			 * select Lane 0/1 to access by APB I/F
			 */
			reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
			reg_data &= ~LANE_ENABLE_MASK;
			reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
			writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

		break;
		case DOWNSTREAM_PORT2:
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_3_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
		break;
		default:
			return -EINVAL;

		}
	break;
	default:
		return -EINVAL;

	}
	writel((ioctl_data.txmargin & 0x1FF),
			ioaddr + TC956X_PHY_COREX_PCS_GL_MD_CFG_TXMARGIN0);

	return 0;
}



static int tc956xmac_set_gen3_deemphasis(struct tc956xmac_priv *priv, void __iomem *ioaddr,
						__u8 txpreset, __u8 enable)
{
	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	if (enable) {
		/*Write Gen1 De-emphasis setting (as of now no configuration through this IOCTL) */
		writel(0x0, ioaddr + TC956X_PHY_COREX_PMA_LN_PCS_TAP_ADV_R0);
		writel(0x0E, ioaddr + TC956X_PHY_COREX_PMA_LN_PCS_TAP_DLY_R0);

		/*Write Gen2 De-emphasis setting (as of now no configuration through this IOCTL) */
		writel(0x0, ioaddr + TC956X_PHY_COREX_PMA_LN_PCS_TAP_ADV_R1);
		writel(0x14, ioaddr + TC956X_PHY_COREX_PMA_LN_PCS_TAP_DLY_R1);

		/*Write Gen3 De-emphasis setting */
		writel(tx_demphasis_setting[txpreset][0], ioaddr + TC956X_PHY_COREX_PMA_LN_PCS_TAP_ADV_R2);
		writel(tx_demphasis_setting[txpreset][1], ioaddr + TC956X_PHY_COREX_PMA_LN_PCS_TAP_DLY_R2);

		/*Set override enable setting */
		writel(0x1, ioaddr + TC956X_PHY_COREX_PMA_LN_RT_OVREN_PCS_TAP_ADV);
		writel(0x1, ioaddr + TC956X_PHY_COREX_PMA_LN_RT_OVREN_PCS_TAP_DLY);
	} else {
		/*Clear override enable setting */
		writel(0x0, ioaddr + TC956X_PHY_COREX_PMA_LN_RT_OVREN_PCS_TAP_ADV);
		writel(0x0, ioaddr + TC956X_PHY_COREX_PMA_LN_RT_OVREN_PCS_TAP_DLY);
	}


	return 0;
}

static int tc956xmac_dfe_read_and_write_ctle_optimised(struct tc956xmac_priv *priv, void __iomem *ioaddr)
{
	u32 reg_data;
	u8 eqc_force, eq_res, vga_ctrl;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	/* Read the optimized CTLE values from correct phy core
	 *(selection should be done before calling this function)
	 */
	reg_data = readl(ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_RESULT_BEST_MON0);
	eqc_force = ((reg_data & 0x780) >> 0x7);
	eq_res = ((reg_data & 0x78) >> 0x3);
	vga_ctrl = (reg_data & 0x7);

	/* Write back the optimized CTLE values from correct phy core
	 * ((selection should be done before calling this function)
	 */
	reg_data = readl(ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);
	reg_data &= ~0xFF7;
	reg_data |= (eqc_force << 0x8 | eq_res << 0x4 | vga_ctrl);
	writel(reg_data, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);

	return 0;
}

static int tc956xmac_dfe_disable_settings(struct tc956xmac_priv *priv, void __iomem *ioaddr, u32 phy_core)
{

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	/*Set DFE Disable settings (1)*/
	writel(0x0, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EQ_CFG0_R2);

	/*Set DFE Disable settings (2)*/
	writel(0x411, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EQ_CFG1_R2);

	/*Set DFE Disable settings (3)*/
	writel(0x11, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EQ1_CFG0_R2);

	/*Set DFE Disable settings (4)*/
	writel(0x11, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EQ2_CFG0_R2);

	/*Set DFE Disable settings (5)*/
	if (phy_core == PHY_CORE_1_ENABLE)
		writel(0x7, ioaddr + TC956X_PHY_CORE1_PMACNT_GL_PM_DFE_PD_CTRL);
	else /*For all other phy core write at offset 0x254*/
		writel(0x7, ioaddr + TC956X_PHY_COREX_PMACNT_GL_PM_DFE_PD_CTRL);

	return 0;
}

static int tc956xmac_dfe_enable_settings(struct tc956xmac_priv *priv, void __iomem *ioaddr, u32 phy_core)
{

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	/*Set DFE Enable settings (1)*/
	writel(0x3, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EQ_CFG0_R2);

	/*Set DFE Enable settings (2)*/
	writel(0x2111, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EQ1_CFG0_R2);

	/*Set DFE Enable settings (3)*/
	writel(0x2111, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EQ2_CFG0_R2);

	/*Set DFE Enable settings (4)*/
	if (phy_core == PHY_CORE_1_ENABLE)
		writel(0x3, ioaddr + TC956X_PHY_CORE1_PMACNT_GL_PM_DFE_PD_CTRL);
	else /*For all other phy core write at offset 0x254*/
		writel(0x3, ioaddr + TC956X_PHY_COREX_PMACNT_GL_PM_DFE_PD_CTRL);

	return 0;
}

static int tc956xmac_dfe_pll_reset_settings(struct tc956xmac_priv *priv, void __iomem *ioaddr, u8 dfe_enable_flag)
{
	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	/*Enable PHY Rate Change Override mode*/
	writel(0x10, ioaddr + TC956X_PHY_COREX_PMACNT_GL_PM_IF_CNT0);

	if (dfe_enable_flag)
		/*Set DFE Enable settings (5)*/
		writel(0x211, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EQ_CFG1_R2);

	/*Execute PHY Rate Change*/
	writel(0x21, ioaddr + TC956X_PHY_COREX_PMACNT_GL_PM_IF_CNT4);

	/*Wait for PHY Rate Change done*/
	mdelay(1);

	/*Clear PHY Rate Change reqeust*/
	writel(0x0, ioaddr + TC956X_PHY_COREX_PMACNT_GL_PM_IF_CNT4);

	/*Disable PHY Rate Change Override mode*/
	writel(0x0, ioaddr + TC956X_PHY_COREX_PMACNT_GL_PM_IF_CNT0);

	return 0;
}

static int tc956xmac_pcie_set_tx_deemphasis(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956x_ioctl_pcie_set_tx_deemphasis ioctl_data;
	u32 reg_data;
	u32 pcie_mode;
	void __iomem *ioaddr;

	ioaddr = priv->ioaddr;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	/* Return error if the interface where lane change request sent is not Port 0 */
	if (priv->port_num != RM_PF0_ID) {
		KPRINT_INFO("Tx Margin change requested through wrong interface\n\r");
		return -EINVAL;
	}

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	/* Read mode setting register
	 * Mode settings values 0:Setting A: x4x1x1, 1:Setting B: x2x2x1
	 */
	reg_data = readl(priv->ioaddr + NMODESTS_OFFSET);
	pcie_mode = (reg_data & NMODESTS_MODE2) >> NMODESTS_MODE2_SHIFT;

	KPRINT_INFO("Pcie mode: %d\n\r", pcie_mode);

	switch (pcie_mode) {
	case TC956X_PCIE_SETTING_A: /* 0:Setting A: x4x1x1 mode */
		switch (ioctl_data.port) {
		case UPSTREAM_PORT:
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= (PHY_CORE_0_ENABLE | PHY_CORE_1_ENABLE);
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			/* Set lane_access_enable Register in PHY PHY_CORE 0 PCS Global Register to
			 * select Lane 0/1 to access by APB I/F
			 */
			reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
			reg_data &= ~LANE_ENABLE_MASK;
			reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
			writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

			tc956xmac_set_gen3_deemphasis(priv, ioaddr, ioctl_data.txpreset, ioctl_data.enable);


		break;
		case DOWNSTREAM_PORT1:
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_2_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			tc956xmac_set_gen3_deemphasis(priv, ioaddr, ioctl_data.txpreset, ioctl_data.enable);


		break;
		case DOWNSTREAM_PORT2:
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_3_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			tc956xmac_set_gen3_deemphasis(priv, ioaddr, ioctl_data.txpreset, ioctl_data.enable);

		break;
		default:
			return -EINVAL;

		}

	break;
	case TC956X_PCIE_SETTING_B: /* 1:Setting B: x2x2x1 mode */
		switch (ioctl_data.port) {
		case UPSTREAM_PORT:
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_0_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			/* Set lane_access_enable Register in PHY PHY_CORE 0 PCS Global Register to
			 * select Lane 0/1 to access by APB I/F
			 */
			reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
			reg_data &= ~LANE_ENABLE_MASK;
			reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
			writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

			tc956xmac_set_gen3_deemphasis(priv, ioaddr, ioctl_data.txpreset, ioctl_data.enable);

		break;
		case DOWNSTREAM_PORT1:
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_1_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			/* Set lane_access_enable Register in PHY PHY_CORE 0 PCS Global Register to
			 * select Lane 0/1 to access by APB I/F
			 */
			reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
			reg_data &= ~LANE_ENABLE_MASK;
			reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
			writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

			tc956xmac_set_gen3_deemphasis(priv, ioaddr, ioctl_data.txpreset, ioctl_data.enable);

		break;
		case DOWNSTREAM_PORT2:
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_3_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			tc956xmac_set_gen3_deemphasis(priv, ioaddr, ioctl_data.txpreset, ioctl_data.enable);

		break;
		default:
			return -EINVAL;
		}
	break;
	default:
		return -EINVAL;

	}

	return 0;
}

static int tc956xmac_pcie_set_dfe(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956x_ioctl_pcie_set_dfe ioctl_data;
	u32 reg_data;
	u32 pcie_mode;
	void __iomem *ioaddr;

	ioaddr = priv->ioaddr;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	/* Return error if the interface where lane change request sent is not Port 0 */
	if (priv->port_num != RM_PF0_ID) {
		KPRINT_INFO("Tx Margin change requested through wrong interface\n\r");
		return -EINVAL;
	}

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	/* Read mode setting register
	 * Mode settings values 0:Setting A: x4x1x1, 1:Setting B: x2x2x1
	 */
	reg_data = readl(priv->ioaddr + NMODESTS_OFFSET);
	pcie_mode = (reg_data & NMODESTS_MODE2) >> NMODESTS_MODE2_SHIFT;

	KPRINT_INFO("Pcie mode: %d\n\r", pcie_mode);

	switch (pcie_mode) {
	case TC956X_PCIE_SETTING_A: /* 0:Setting A: x4x1x1 mode */
		switch (ioctl_data.port) {
		case UPSTREAM_PORT:
			/*Device should be in Gen3 when we execute this command*/
			reg_data = readl(ioaddr + TC956X_GLUE_TL_LINK_SPEED_MON);
			if ((reg_data & 0xF) != 0x3) {
				KPRINT_INFO("Device not in Gen3 to execute DFE disable\n\r");
				return -EINVAL;
			}

			if (ioctl_data.enable == 0) { /*DFE Disable settings*/

				/*Phy core 0, Lane 0 CTLE copy settings*/
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= PHY_CORE_0_ENABLE;
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
				reg_data &= ~LANE_ENABLE_MASK;
				reg_data |= LANE_0_ENABLE;
				writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

				tc956xmac_dfe_read_and_write_ctle_optimised(priv, ioaddr);


				/*Phy core 0, Lane 1 CTLE copy settings*/
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= PHY_CORE_0_ENABLE;
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
				reg_data &= ~LANE_ENABLE_MASK;
				reg_data |= LANE_1_ENABLE;
				writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

				tc956xmac_dfe_read_and_write_ctle_optimised(priv, ioaddr);


				/*Phy core 1, Lane 0 CTLE copysettings*/
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= PHY_CORE_1_ENABLE;
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
				reg_data &= ~LANE_ENABLE_MASK;
				reg_data |= LANE_0_ENABLE;
				writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

				tc956xmac_dfe_read_and_write_ctle_optimised(priv, ioaddr);


				/*Phy core 1, Lane 1 CTLE copy settings*/
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= PHY_CORE_1_ENABLE;
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
				reg_data &= ~LANE_ENABLE_MASK;
				reg_data |= LANE_1_ENABLE;
				writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

				tc956xmac_dfe_read_and_write_ctle_optimised(priv, ioaddr);

				/*Disable DFE settings for Phy core0 and Lane 0/1*/
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= PHY_CORE_0_ENABLE;
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
				reg_data &= ~LANE_ENABLE_MASK;
				reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
				writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

				tc956xmac_dfe_disable_settings(priv, ioaddr, PHY_CORE_0_ENABLE);

				/*Disable DFE settings for Phy core1 and Lane 0/1*/
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= PHY_CORE_1_ENABLE;
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
				reg_data &= ~LANE_ENABLE_MASK;
				reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
				writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

				tc956xmac_dfe_disable_settings(priv, ioaddr, PHY_CORE_1_ENABLE);

				/*PLL reset settings for phy core 0/1*/
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= (PHY_CORE_0_ENABLE | PHY_CORE_1_ENABLE);
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				tc956xmac_dfe_pll_reset_settings(priv, ioaddr, ioctl_data.enable);
			} else { /*DFE Enable settings*/

				/*Phy core 0/1, Lane 0/1 CTLE initial value register settings*/
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= (PHY_CORE_0_ENABLE | PHY_CORE_1_ENABLE);
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
				reg_data &= ~LANE_ENABLE_MASK;
				reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
				writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

				writel(0x000008A5, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);


				/*Enable DFE settings for Phy core0 */
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= PHY_CORE_0_ENABLE;
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				tc956xmac_dfe_enable_settings(priv, ioaddr, PHY_CORE_0_ENABLE);


				/*Enable DFE settings for Phy core1 */
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= PHY_CORE_1_ENABLE;
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				tc956xmac_dfe_enable_settings(priv, ioaddr, PHY_CORE_1_ENABLE);


				/*PLL reset settings for Phy core0/1*/
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= (PHY_CORE_0_ENABLE | PHY_CORE_1_ENABLE);
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				tc956xmac_dfe_pll_reset_settings(priv, ioaddr, ioctl_data.enable);

			}

		break;
		case DOWNSTREAM_PORT1:
			/*Device should be in Gen3 when we execute this command*/
			reg_data = readl(ioaddr + TC956X_GLUE_TL_LINK_SPEED_MON);
			if ((reg_data & 0xF00) != 0x300) {
				KPRINT_INFO("Device not in Gen3 to execute DFE disable\n\r");
				return -EINVAL;
			}

			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_2_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);


			if (ioctl_data.enable == 0) { /*DFE Disable settings*/
				tc956xmac_dfe_read_and_write_ctle_optimised(priv, ioaddr);

				/*Disable DFE settings for Phy core2*/
				tc956xmac_dfe_disable_settings(priv, ioaddr, PHY_CORE_2_ENABLE);

				/*PLL reset settings for Phy core2*/
				tc956xmac_dfe_pll_reset_settings(priv, ioaddr, ioctl_data.enable);
			} else { /*DFE Enable settings*/
				writel(0x000008A5, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);

				/*Enable DFE settings for Phy core2*/
				tc956xmac_dfe_enable_settings(priv, ioaddr, PHY_CORE_2_ENABLE);

				/*PLL reset settings for Phy core2*/
				tc956xmac_dfe_pll_reset_settings(priv, ioaddr, ioctl_data.enable);
			}
		break;
		case DOWNSTREAM_PORT2:
			/*Device should be in Gen3 when we execute this command*/
			reg_data = readl(ioaddr + TC956X_GLUE_TL_LINK_SPEED_MON);
			if ((reg_data & 0xF0000) != 0x30000) {
				KPRINT_INFO("Device not in Gen3 to execute DFE disable\n\r");
				return -EINVAL;
			}

			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_3_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);


			if (ioctl_data.enable == 0) { /*DFE Disable settings*/
				tc956xmac_dfe_read_and_write_ctle_optimised(priv, ioaddr);

				/*Disable DFE settings for Phy core3*/
				tc956xmac_dfe_disable_settings(priv, ioaddr, PHY_CORE_3_ENABLE);

				/*PLL reset settings for Phy core3*/
				tc956xmac_dfe_pll_reset_settings(priv, ioaddr, ioctl_data.enable);
			} else {  /*DFE Enable settings*/
				writel(0x000008A5, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);

				/*Enable DFE settings for Phy core2*/
				tc956xmac_dfe_enable_settings(priv, ioaddr, PHY_CORE_3_ENABLE);

				/*PLL reset settings for Phy core2*/
				tc956xmac_dfe_pll_reset_settings(priv, ioaddr, ioctl_data.enable);
			}

		break;
		default:
			return -EINVAL;

		}

	break;
	case TC956X_PCIE_SETTING_B: /* 1:Setting B: x2x2x1 mode */
		switch (ioctl_data.port) {
		case UPSTREAM_PORT:
			/*Device should be in Gen3 when we execute this command*/
			reg_data = readl(ioaddr + TC956X_GLUE_TL_LINK_SPEED_MON);
			if ((reg_data & 0xF) != 0x3) {
				KPRINT_INFO("Device not in Gen3 to execute DFE disable\n\r");
				return -EINVAL;
			}

			if (ioctl_data.enable == 0) {//Disable setting
				/*Phy core 0, Lane 0 CTLE copy settings*/
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= PHY_CORE_0_ENABLE;
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
				reg_data &= ~LANE_ENABLE_MASK;
				reg_data |= LANE_0_ENABLE;
				writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

				tc956xmac_dfe_read_and_write_ctle_optimised(priv, ioaddr);


				/*Phy core 0, Lane 1 CTLE copy settings*/
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= PHY_CORE_0_ENABLE;
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
				reg_data &= ~LANE_ENABLE_MASK;
				reg_data |= LANE_1_ENABLE;
				writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

				tc956xmac_dfe_read_and_write_ctle_optimised(priv, ioaddr);


				/*Disable DFE settings for Phy core0 and Lane 0/1*/
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= PHY_CORE_0_ENABLE;
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
				reg_data &= ~LANE_ENABLE_MASK;
				reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
				writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

				tc956xmac_dfe_disable_settings(priv, ioaddr, PHY_CORE_0_ENABLE);

				/*PLL reset settings*/
				tc956xmac_dfe_pll_reset_settings(priv, ioaddr, ioctl_data.enable);
			} else { /*Enable settings*/

				/*Phy core 0, Lane 0/1 CTLE initial value register settings*/
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= PHY_CORE_0_ENABLE;
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
				reg_data &= ~LANE_ENABLE_MASK;
				reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
				writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

				writel(0x000008A5, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);

				tc956xmac_dfe_enable_settings(priv, ioaddr, PHY_CORE_0_ENABLE);

				/*PLL reset settings*/
				tc956xmac_dfe_pll_reset_settings(priv, ioaddr, ioctl_data.enable);

			}
		break;
		case DOWNSTREAM_PORT1:
			/*Device should be in Gen3 when we execute this command*/
			reg_data = readl(ioaddr + TC956X_GLUE_TL_LINK_SPEED_MON);
			if ((reg_data & 0xF00) != 0x300) {
				KPRINT_INFO("Device not in Gen3 to execute DFE disable\n\r");
				return -EINVAL;
			}

			if (ioctl_data.enable == 0) {//Disable setting
				/*Phy core 1, Lane 0 CTLE copy settings*/
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= PHY_CORE_1_ENABLE;
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
				reg_data &= ~LANE_ENABLE_MASK;
				reg_data |= LANE_0_ENABLE;
				writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

				tc956xmac_dfe_read_and_write_ctle_optimised(priv, ioaddr);


				/*Phy core 1, Lane 1 CTLE copy settings*/
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= PHY_CORE_1_ENABLE;
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
				reg_data &= ~LANE_ENABLE_MASK;
				reg_data |= LANE_1_ENABLE;
				writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

				tc956xmac_dfe_read_and_write_ctle_optimised(priv, ioaddr);


				/*Disable DFE settings for Phy core1*/
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= PHY_CORE_1_ENABLE;
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
				reg_data &= ~LANE_ENABLE_MASK;
				reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
				writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

				tc956xmac_dfe_disable_settings(priv, ioaddr, PHY_CORE_1_ENABLE);

				/*PLL reset settings for Phy core1*/
				tc956xmac_dfe_pll_reset_settings(priv, ioaddr, ioctl_data.enable);
			} else { /*DFE Enable settings*/
				/*Phy core 1, Lane 0 CTLE copy settings*/
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= PHY_CORE_1_ENABLE;
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
				reg_data &= ~LANE_ENABLE_MASK;
				reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
				writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

				writel(0x000008A5, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);

				tc956xmac_dfe_enable_settings(priv, ioaddr, PHY_CORE_1_ENABLE);

				/*PLL reset settings*/
				tc956xmac_dfe_pll_reset_settings(priv, ioaddr, ioctl_data.enable);

			}
		break;
		case DOWNSTREAM_PORT2:
			/*Device should be in Gen3 when we execute this command*/
			reg_data = readl(ioaddr + TC956X_GLUE_TL_LINK_SPEED_MON);
			if ((reg_data & 0xF0000) != 0x30000) {
				KPRINT_INFO("Device not in Gen3 to execute DFE disable\n\r");
				return -EINVAL;
			}

			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_3_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			if (ioctl_data.enable == 0) {//Disable setting

				tc956xmac_dfe_read_and_write_ctle_optimised(priv, ioaddr);


				/*Disable DFE settings for Phy core3*/
				tc956xmac_dfe_disable_settings(priv, ioaddr, PHY_CORE_3_ENABLE);

				/*PLL reset settings for Phy core3*/
				tc956xmac_dfe_pll_reset_settings(priv, ioaddr, ioctl_data.enable);
			} else { /*DFE Enable settings*/
				writel(0x000008A5, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);

				tc956xmac_dfe_enable_settings(priv, ioaddr, PHY_CORE_3_ENABLE);

				/*PLL reset settings*/
				tc956xmac_dfe_pll_reset_settings(priv, ioaddr, ioctl_data.enable);

			}
		break;
		default:
			return -EINVAL;

		}
	break;
	default:
		return -EINVAL;

	}

	return 0;
}

static int tc956xmac_pcie_set_ctle_fixed(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956x_ioctl_pcie_set_ctle_fixed_mode ioctl_data;
	u32 reg_data;
	u32 pcie_mode;
	void __iomem *ioaddr;

	ioaddr = priv->ioaddr;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	/* Return error if the interface where lane change request sent is not Port 0 */
	if (priv->port_num != RM_PF0_ID) {
		KPRINT_INFO("Tx Margin change requested through wrong interface\n\r");
		return -EINVAL;
	}

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	/* Read mode setting register
	 * Mode settings values 0:Setting A: x4x1x1, 1:Setting B: x2x2x1
	 */
	reg_data = readl(priv->ioaddr + NMODESTS_OFFSET);
	pcie_mode = (reg_data & NMODESTS_MODE2) >> NMODESTS_MODE2_SHIFT;

	KPRINT_INFO("Pcie mode: %d\n\r", pcie_mode);

	switch (pcie_mode) {
	case TC956X_PCIE_SETTING_A: /* 0:Setting A: x4x1x1 mode */
		switch (ioctl_data.port) {
		case UPSTREAM_PORT:
			/*Device should be in Gen3 when we execute this command*/
			reg_data = readl(ioaddr + TC956X_GLUE_TL_LINK_SPEED_MON);
			if ((reg_data & 0xF) != 0x3) {
				KPRINT_INFO("Device not in Gen3 to execute DFE disable\n\r");
				return -EINVAL;
			}

			/*Phy core 0, Lane 0/1 */
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_0_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
			reg_data &= ~LANE_ENABLE_MASK;
			reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
			writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

			/*Set CTLE initial value registers*/
			reg_data = readl(ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);
			reg_data &= ~0xFF7;
			reg_data |= ((ioctl_data.eqc_force & 0xF) << 0x8 |
				     (ioctl_data.eq_res & 0xF) << 0x4 |
				     (ioctl_data.vga_ctrl & 0x7));
			writel(reg_data, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);


			/*Phy core 1, Lane 0/1 */
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_1_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
			reg_data &= ~LANE_ENABLE_MASK;
			reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
			writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

			/*Set CTLE initial value registers*/
			reg_data = readl(ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);
			reg_data &= ~0xFF7;
			reg_data |= ((ioctl_data.eqc_force & 0xF) << 0x8 |
				     (ioctl_data.eq_res & 0xF) << 0x4 |
				     (ioctl_data.vga_ctrl & 0x7));
			writel(reg_data, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);


			/*Disable DFE settings for Phy core0 and Lane 0/1*/
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_0_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
			reg_data &= ~LANE_ENABLE_MASK;
			reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
			writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

			tc956xmac_dfe_disable_settings(priv, ioaddr, PHY_CORE_0_ENABLE);

			/*Disable DFE settings for Phy core1 and Lane 0/1*/
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_1_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
			reg_data &= ~LANE_ENABLE_MASK;
			reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
			writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

			tc956xmac_dfe_disable_settings(priv, ioaddr, PHY_CORE_1_ENABLE);

			/*PLL reset settings for phy core 0/1*/
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= (PHY_CORE_0_ENABLE | PHY_CORE_1_ENABLE);
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			tc956xmac_dfe_pll_reset_settings(priv, ioaddr, 0);

		break;
		case DOWNSTREAM_PORT1:
			/*Device should be in Gen3 when we execute this command*/
			reg_data = readl(ioaddr + TC956X_GLUE_TL_LINK_SPEED_MON);
			if ((reg_data & 0xF00) != 0x300) {
				KPRINT_INFO("Device not in Gen3 to execute DFE disable\n\r");
				return -EINVAL;
			}

			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_2_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);


			/*Set CTLE initial value registers*/
			reg_data = readl(ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);
			reg_data &= ~0xFF7;
			reg_data |= ((ioctl_data.eqc_force & 0xF) << 0x8 |
				     (ioctl_data.eq_res & 0xF) << 0x4 |
				     (ioctl_data.vga_ctrl & 0x7));
			writel(reg_data, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);

			/*Disable DFE settings for Phy core2*/
			tc956xmac_dfe_disable_settings(priv, ioaddr, PHY_CORE_2_ENABLE);

			/*PLL reset settings for Phy core2*/
			tc956xmac_dfe_pll_reset_settings(priv, ioaddr, 0);
		break;
		case DOWNSTREAM_PORT2:
			/*Device should be in Gen3 when we execute this command*/
			reg_data = readl(ioaddr + TC956X_GLUE_TL_LINK_SPEED_MON);
			if ((reg_data & 0xF0000) != 0x30000) {
				KPRINT_INFO("Device not in Gen3 to execute DFE disable\n\r");
				return -EINVAL;
			}

			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_3_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			/*Set CTLE initial value registers*/
			reg_data = readl(ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);
			reg_data &= ~0xFF7;
			reg_data |= ((ioctl_data.eqc_force & 0xF) << 0x8 |
				     (ioctl_data.eq_res & 0xF) << 0x4 |
				     (ioctl_data.vga_ctrl & 0x7));
			writel(reg_data, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);

			/*Disable DFE settings for Phy core3*/
			tc956xmac_dfe_disable_settings(priv, ioaddr, PHY_CORE_3_ENABLE);

			/*PLL reset settings for Phy core3*/
			tc956xmac_dfe_pll_reset_settings(priv, ioaddr, 0);

		break;
		default:
			return -EINVAL;

		}

	break;
	case TC956X_PCIE_SETTING_B: /* 1:Setting B: x2x2x1 mode */
		switch (ioctl_data.port) {
		case UPSTREAM_PORT:
			/*Device should be in Gen3 when we execute this command*/
			reg_data = readl(ioaddr + TC956X_GLUE_TL_LINK_SPEED_MON);
			if ((reg_data & 0xF) != 0x3) {
				KPRINT_INFO("Device not in Gen3 to execute DFE disable\n\r");
				return -EINVAL;
			}

			/*Phy core 0, Lane 0 */
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_0_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
			reg_data &= ~LANE_ENABLE_MASK;
			reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
			writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);


			/*Set CTLE initial value registers*/
			reg_data = readl(ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);
			reg_data &= ~0xFF7;
			reg_data |= ((ioctl_data.eqc_force & 0xF) << 0x8 |
				     (ioctl_data.eq_res & 0xF) << 0x4 |
				     (ioctl_data.vga_ctrl & 0x7));
			writel(reg_data, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);

			/*Disable DFE settings for Phy core0 and Lane 0/1*/
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_0_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
			reg_data &= ~LANE_ENABLE_MASK;
			reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
			writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

			tc956xmac_dfe_disable_settings(priv, ioaddr, PHY_CORE_0_ENABLE);

			/*PLL reset settings done with disable flag*/
			tc956xmac_dfe_pll_reset_settings(priv, ioaddr, 0);

		break;
		case DOWNSTREAM_PORT1:
			/*Device should be in Gen3 when we execute this command*/
			reg_data = readl(ioaddr + TC956X_GLUE_TL_LINK_SPEED_MON);
			if ((reg_data & 0xF00) != 0x300) {
				KPRINT_INFO("Device not in Gen3 to execute DFE disable\n\r");
				return -EINVAL;
			}

			/*Phy core 1, Lane 0 */
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_1_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
			reg_data &= ~LANE_ENABLE_MASK;
			reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
			writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

			/*Set CTLE initial value registers*/
			reg_data = readl(ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);
			reg_data &= ~0xFF7;
			reg_data |= ((ioctl_data.eqc_force & 0xF) << 0x8 |
				     (ioctl_data.eq_res & 0xF) << 0x4 |
				     (ioctl_data.vga_ctrl & 0x7));
			writel(reg_data, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);

			/*Disable DFE settings for Phy core1*/
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_1_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
			reg_data &= ~LANE_ENABLE_MASK;
			reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
			writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

			tc956xmac_dfe_disable_settings(priv, ioaddr, PHY_CORE_1_ENABLE);

			/*PLL reset settings for Phy core1*/
			tc956xmac_dfe_pll_reset_settings(priv, ioaddr, 0);
		break;
		case DOWNSTREAM_PORT2:
			/*Device should be in Gen3 when we execute this command*/
			reg_data = readl(ioaddr + TC956X_GLUE_TL_LINK_SPEED_MON);
			if ((reg_data & 0xF0000) != 0x30000) {
				KPRINT_INFO("Device not in Gen3 to execute DFE disable\n\r");
				return -EINVAL;
			}

			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_3_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			/*Set CTLE initial value registers*/
			reg_data = readl(ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);
			reg_data &= ~0xFF7;
			reg_data |= ((ioctl_data.eqc_force & 0xF) << 0x8 |
				     (ioctl_data.eq_res & 0xF) << 0x4 |
				     (ioctl_data.vga_ctrl & 0x7));
			writel(reg_data, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);

			/*Disable DFE settings for Phy core3*/
			tc956xmac_dfe_disable_settings(priv, ioaddr, PHY_CORE_3_ENABLE);

			/*PLL reset settings for Phy core3*/
			tc956xmac_dfe_pll_reset_settings(priv, ioaddr, 0);
		break;
		default:
			return -EINVAL;

		}
	break;
	default:
		return -EINVAL;

	}

	return 0;
}

static int tc956xmac_pcie_speed_change(struct tc956xmac_priv *priv, void __user *data)
{
	struct pci_dev *pdev = to_pci_dev(priv->device);
	struct tc956x_ioctl_pcie_set_speed ioctl_data;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	return tc956x_set_pci_speed(pdev, (u32)ioctl_data.speed);
}

#endif /*#define TC956X*/
static int tc956xmac_extension_ioctl(struct tc956xmac_priv *priv,
				     void __user *data)
{
	u32 cmd;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);
	if (!capable(CAP_NET_ADMIN))
		return -EPERM;
	if (copy_from_user(&cmd, data, sizeof(cmd)))
		return -EFAULT;

	switch (cmd) {
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
	case TC956XMAC_GET_CBS:
		return tc956xmac_ioctl_get_cbs(priv, data);
	case TC956XMAC_SET_CBS:
		return tc956xmac_ioctl_set_cbs(priv, data);
	case TC956XMAC_GET_EST:
		return tc956xmac_ioctl_get_est(priv, data);
	case TC956XMAC_SET_EST:
		return tc956xmac_ioctl_set_est(priv, data);
	case TC956XMAC_GET_FPE: /*Function to Get FPE related configurations*/
		return tc956xmac_ioctl_get_fpe(priv, data);
	case TC956XMAC_SET_FPE: /*Function to Set FPE related configurations*/
		return tc956xmac_ioctl_set_fpe(priv, data);
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */
	case TC956X_GET_FW_STATUS:
		return tc956x_xgmac_get_fw_status(priv, data);
	case TC956XMAC_VLAN_FILTERING:
		return tc956xmac_config_vlan_filter(priv, data);
	case TC956XMAC_GET_RXP:
		return tc956xmac_ioctl_get_rxp(priv, data);
	case TC956XMAC_SET_RXP:
		return tc956xmac_ioctl_set_rxp(priv, data);
	case TC956XMAC_GET_SPEED:
		return tc956xmac_ioctl_get_connected_speed(priv, data);
	case TC956XMAC_GET_TX_FREE_DESC:
		return tc956xmac_ioctl_get_tx_free_desc(priv, data);
#ifdef TC956X_IOCTL_REG_RD_WR_ENABLE
	case TC956XMAC_REG_RD:
		return tc956xmac_reg_rd(priv, data);
	case TC956XMAC_REG_WR:
		return tc956xmac_reg_wr(priv, data);
#endif
	case TC956XMAC_SET_MAC_LOOPBACK:
		return tc956xmac_ioctl_set_mac_loopback(priv, data);
	case TC956XMAC_SET_PHY_LOOPBACK:
		return tc956xmac_ioctl_set_phy_loopback(priv, data);
	case TC956XMAC_L2_DA_FILTERING_CMD:
		return tc956xmac_config_l2_da_filter(priv, data);
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
	case TC956XMAC_SET_PPS_OUT:
		return tc956xmac_set_ppsout(priv, data);
	case TC956XMAC_PTPCLK_CONFIG:
		return tc956xmac_ptp_clk_config(priv, data);
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */
	case TC956XMAC_SA0_VLAN_INS_REP_REG:
		return tc956xmac_sa_vlan_ins_config(priv, data);
	case TC956XMAC_SA1_VLAN_INS_REP_REG:
		return tc956xmac_sa_vlan_ins_config(priv, data);
	case TC956XMAC_GET_TX_QCNT:
		return tc956xmac_get_tx_qcnt(priv, data);
	case TC956XMAC_GET_RX_QCNT:
		return tc956xmac_get_rx_qcnt(priv, data);
	case TC956XMAC_PCIE_CONFIG_REG_RD:
		return tc956xmac_pcie_config_reg_rd(priv, data);
	case TC956XMAC_PCIE_CONFIG_REG_WR:
		return tc956xmac_pcie_config_reg_wr(priv, data);
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
	case TC956XMAC_PTPOFFLOADING:
		return tc956xmac_config_ptpoffload(priv, data);
	case TC956XMAC_ENABLE_AUX_TIMESTAMP:
		return tc956xmac_aux_timestamp_enable(priv, data);
	case TC956XMAC_ENABLE_ONESTEP_TIMESTAMP:
		return tc956xmac_config_onestep_timestamp(priv, data);
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */
#ifdef TC956X_PCIE_LOGSTAT
	case TC956X_PCIE_SET_LOGSTAT_CONF:
		return tc956x_pcie_ioctl_SetDbgConf(priv, data);
	case TC956X_PCIE_GET_LOGSTAT_CONF:
		return tc956x_pcie_ioctl_GetDbgConf(priv, data);
	case TC956X_PCIE_GET_LTSSM_LOG:
		return tc956x_pcie_ioctl_GetLTSSMLogD(priv, data);
#endif /* #ifdef TC956X_PCIE_LOGSTAT */
#ifndef TC956X
	case TC956XMAC_VLAN_STRIP_CONFIG:
		return tc956xmac_vlan_strip_config(priv, data);
#endif
#ifdef TC956X
	case TC956XMAC_PCIE_LANE_CHANGE:
		return tc956xmac_pcie_lane_change(priv, data);
	case TC956XMAC_PCIE_SET_TX_MARGIN:
		return tc956xmac_pcie_set_tx_margin(priv, data);
	case TC956XMAC_PCIE_SET_TX_DEEMPHASIS:
		return tc956xmac_pcie_set_tx_deemphasis(priv, data);
	case TC956XMAC_PCIE_SET_DFE:
		return tc956xmac_pcie_set_dfe(priv, data);
	case TC956XMAC_PCIE_SET_CTLE:
		return tc956xmac_pcie_set_ctle_fixed(priv, data);
	case TC956XMAC_PCIE_SPEED_CHANGE:
		return tc956xmac_pcie_speed_change(priv, data);
#endif

	default:
		return -EINVAL;
	}

	return 0;
}

static int tc956xmac_phy_fw_flash_mdio_ioctl(struct net_device *ndev,
					     struct ifreq *ifr, int cmd)
{
	struct mii_ioctl_data *mii = if_mii(ifr);
	struct tc956xmac_priv *priv = netdev_priv(ndev);

	int ret = -EINVAL;
	int prtad, devad;

	if (mdio_phy_id_is_c45(mii->phy_id)) {
		prtad = mdio_phy_id_prtad(mii->phy_id);
		devad = mdio_phy_id_devad(mii->phy_id);
		devad = MII_ADDR_C45 | devad << 16 | mii->reg_num;
	} else {
		prtad = mii->phy_id;
		devad = mii->reg_num;
	}

	switch (cmd) {
	case SIOCGMIIPHY:
		mii->phy_id = 0;
		/* fall through */

	case SIOCGMIIREG:
		ret = priv->mii->read(priv->mii, prtad, devad);
		if (ret >= 0) {
			mii->val_out = ret;
			ret = 0;
		}
		break;

	case SIOCSMIIREG:
		ret = priv->mii->write(priv->mii, prtad, devad,
					mii->val_in);
		break;

	default:
		break;
	}

	return ret;
}

/**
 *  tc956xmac_ioctl - Entry point for the Ioctl
 *  @dev: Device pointer.
 *  @rq: An IOCTL specefic structure, that can contain a pointer to
 *  a proprietary structure used to pass information to the driver.
 *  @cmd: IOCTL command
 *  Description:
 *  Currently it supports the phy_mii_ioctl(...) and HW time stamping.
 */
static int tc956xmac_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
	int ret = -EOPNOTSUPP;
	struct mii_ioctl_data *data = if_mii(rq);

	if (!netif_running(dev))
		return tc956xmac_phy_fw_flash_mdio_ioctl(dev, rq, cmd);

	switch (cmd) {
	case SIOCGMIIPHY:
		data->phy_id = priv->plat->phy_addr;
		NMSGPR_ALERT(priv->device, "PHY ID: SIOCGMIIPHY\n");
		ret = 0;
		break;
	case SIOCGMIIREG:
	case SIOCSMIIREG:
		ret = phylink_mii_ioctl(priv->phylink, rq, cmd);
		break;
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
	case SIOCSHWTSTAMP:
		ret = tc956xmac_hwtstamp_set(dev, rq);
		break;
	case SIOCGHWTSTAMP:
		ret = tc956xmac_hwtstamp_get(dev, rq);
		break;
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */
	case SIOCSTIOCTL:
		if (!priv || !rq)
			return -EINVAL;
		ret = tc956xmac_extension_ioctl(priv, rq->ifr_data);
		break;
	default:
		break;
	}

	return ret;
}

static int tc956xmac_setup_tc_block_cb(enum tc_setup_type type, void *type_data,
				    void *cb_priv)
{
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
	struct tc956xmac_priv *priv = cb_priv;
	int ret = -EOPNOTSUPP;

	if (!tc_cls_can_offload_and_chain0(priv->dev, type_data))
		return ret;

	tc956xmac_disable_all_queues(priv);

	switch (type) {
	case TC_SETUP_CLSU32:
		ret = tc956xmac_tc_setup_cls_u32(priv, type_data);
		break;
	case TC_SETUP_CLSFLOWER:
		ret = tc956xmac_tc_setup_cls(priv, type_data);
		break;
	default:
		break;
	}

	tc956xmac_enable_all_queues(priv);
	return ret;
#else
	return 0;
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */

}

static LIST_HEAD(tc956xmac_block_cb_list);

static int tc956xmac_setup_tc(struct net_device *ndev, enum tc_setup_type type,
			   void *type_data)
{
	struct tc956xmac_priv *priv = netdev_priv(ndev);

	switch (type) {
	case TC_SETUP_BLOCK:
		return flow_block_cb_setup_simple(type_data,
						  &tc956xmac_block_cb_list,
						  tc956xmac_setup_tc_block_cb,
						  priv, priv, true);
	case TC_SETUP_QDISC_CBS:
		return tc956xmac_tc_setup_cbs(priv, type_data);
	case TC_SETUP_QDISC_TAPRIO:
		return tc956xmac_tc_setup_taprio(priv, type_data);
	case TC_SETUP_QDISC_ETF:
		return tc956xmac_tc_setup_etf(priv, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

static u16 tc956xmac_select_queue(struct net_device *dev, struct sk_buff *skb,
			       struct net_device *sb_dev)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
#ifdef TC956X
	u16 txqueue_select;
	unsigned int eth_or_vlan_tag;
	unsigned int eth_type;
	unsigned int avb_priority;

	/* TX Channel assignment based on Vlan tag and protocol type */
	eth_or_vlan_tag = htons(((skb->data[13]<<8) | skb->data[12]));
	if (eth_or_vlan_tag == ETH_P_8021Q) {
		eth_type = htons(((skb->data[17]<<8) | skb->data[16]));
		if (eth_type == ETH_P_TSN) {
			/* Extract VLAN priority field from the tx data */
			avb_priority = htons((skb->data[15]<<8) | skb->data[14]);

			avb_priority >>= 13;
			if (avb_priority == TC956X_AVB_PRIORITY_CLASS_A)
				txqueue_select = AVB_CLASS_A_TX_CH;
			else if (avb_priority == TC956X_AVB_PRIORITY_CLASS_B)
				txqueue_select = AVB_CLASS_B_TX_CH;
			else if (avb_priority == TC956X_PRIORITY_CLASS_CDT)
				txqueue_select = TSN_CLASS_CDT_TX_CH;
			else
				txqueue_select = HOST_BEST_EFF_CH;
		} else {
			txqueue_select = LEGACY_VLAN_TAGGED_CH;
		}
	} else {
		switch (eth_or_vlan_tag) {
		case ETH_P_1588:
			txqueue_select = TC956X_GPTP_TX_CH;
			break;
		default:
			txqueue_select = HOST_BEST_EFF_CH;
			break;
		}
	}
	netdev_dbg(priv->dev, "%s: Tx Queue select = %d", __func__, txqueue_select);
	return txqueue_select;
#else
	int gso = skb_shinfo(skb)->gso_type;

	if (gso & (SKB_GSO_TCPV4 | SKB_GSO_TCPV6 | SKB_GSO_UDP_L4)) {
		/*
		 * There is no way to determine the number of TSO/USO
		 * capable Queues. Let's use always the Queue 0
		 * because if TSO/USO is supported then at least this
		 * one will be capable.
		 */
		return 0;
	}

	return netdev_pick_tx(dev, skb, NULL) % dev->real_num_tx_queues;
#endif
}

static int tc956xmac_set_mac_address(struct net_device *ndev, void *addr)
{
	struct tc956xmac_priv *priv = netdev_priv(ndev);
	int ret = 0;

	ret = eth_mac_addr(ndev, addr);
	if (ret)
		return ret;

	tc956xmac_set_umac_addr(priv, priv->hw, ndev->dev_addr, HOST_MAC_ADDR_OFFSET, PF_DRIVER);

	return ret;
}

#ifdef CONFIG_DEBUG_FS
static struct dentry *tc956xmac_fs_dir;

#ifdef TC956X_UNSUPPORTED_UNTESETD_FEATURE
static void sysfs_display_ring(void *head, int size, int extend_desc,
			       struct seq_file *seq)
{
	int i;
	struct dma_extended_desc *ep = (struct dma_extended_desc *)head;
	struct dma_desc *p = (struct dma_desc *)head;

	for (i = 0; i < size; i++) {
		if (extend_desc) {
			seq_printf(seq, "%d [0x%x]: 0x%x 0x%x 0x%x 0x%x\n",
				   i, (unsigned int)virt_to_phys(ep),
				   le32_to_cpu(ep->basic.des0),
				   le32_to_cpu(ep->basic.des1),
				   le32_to_cpu(ep->basic.des2),
				   le32_to_cpu(ep->basic.des3));
			ep++;
		} else {
			seq_printf(seq, "%d [0x%x]: 0x%x 0x%x 0x%x 0x%x\n",
				   i, (unsigned int)virt_to_phys(p),
				   le32_to_cpu(p->des0), le32_to_cpu(p->des1),
				   le32_to_cpu(p->des2), le32_to_cpu(p->des3));
			p++;
		}
		seq_printf(seq, "\n");
	}
}

static int tc956xmac_rings_status_show(struct seq_file *seq, void *v)
{
	struct net_device *dev = seq->private;
	struct tc956xmac_priv *priv = netdev_priv(dev);
	u32 rx_count = priv->plat->rx_queues_to_use;
	u32 tx_count = priv->plat->tx_queues_to_use;
	u32 queue;

	if ((dev->flags & IFF_UP) == 0)
		return 0;

	for (queue = 0; queue < rx_count; queue++) {
		struct tc956xmac_rx_queue *rx_q = &priv->rx_queue[queue];

		if (priv->plat->rx_dma_ch_owner[queue] != USE_IN_TC956X_SW)
			continue;

		seq_printf(seq, "RX Queue %d:\n", queue);

		if (priv->extend_desc) {
			seq_printf(seq, "Extended descriptor ring:\n");
			sysfs_display_ring((void *)rx_q->dma_erx,
					   DMA_RX_SIZE, 1, seq);
		} else {
			seq_printf(seq, "Descriptor ring:\n");
			sysfs_display_ring((void *)rx_q->dma_rx,
					   DMA_RX_SIZE, 0, seq);
		}
	}

	for (queue = 0; queue < tx_count; queue++) {
		struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[queue];

		if (priv->plat->tx_dma_ch_owner[queue] != USE_IN_TC956X_SW)
			continue;

		seq_printf(seq, "TX Queue %d:\n", queue);

		if (priv->extend_desc) {
			seq_printf(seq, "Extended descriptor ring:\n");
			sysfs_display_ring((void *)tx_q->dma_etx,
					   DMA_TX_SIZE, 1, seq);
		} else if (!(tx_q->tbs & TC956XMAC_TBS_AVAIL)) {
			seq_printf(seq, "Descriptor ring:\n");
			sysfs_display_ring((void *)tx_q->dma_tx,
					   DMA_TX_SIZE, 0, seq);
		}
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(tc956xmac_rings_status);

static int tc956xmac_dma_cap_show(struct seq_file *seq, void *v)
{
	struct net_device *dev = seq->private;
	struct tc956xmac_priv *priv = netdev_priv(dev);

	if (!priv->hw_cap_support) {
		seq_printf(seq, "DMA HW features not supported\n");
		return 0;
	}

	seq_printf(seq, "==============================\n");
	seq_printf(seq, "\tDMA HW features\n");
	seq_printf(seq, "==============================\n");

	seq_printf(seq, "\t10/100 Mbps: %s\n",
		   (priv->dma_cap.mbps_10_100) ? "Y" : "N");
	seq_printf(seq, "\t1000 Mbps: %s\n",
		   (priv->dma_cap.mbps_1000) ? "Y" : "N");
	seq_printf(seq, "\tHalf duplex: %s\n",
		   (priv->dma_cap.half_duplex) ? "Y" : "N");
	seq_printf(seq, "\tHash Filter: %s\n",
		   (priv->dma_cap.hash_filter) ? "Y" : "N");
	seq_printf(seq, "\tMultiple MAC address registers: %s\n",
		   (priv->dma_cap.multi_addr) ? "Y" : "N");
	seq_printf(seq, "\tPCS (TBI/SGMII/RTBI PHY interfaces): %s\n",
		   (priv->dma_cap.pcs) ? "Y" : "N");
	seq_printf(seq, "\tSMA (MDIO) Interface: %s\n",
		   (priv->dma_cap.sma_mdio) ? "Y" : "N");
	seq_printf(seq, "\tPMT Remote wake up: %s\n",
		   (priv->dma_cap.pmt_remote_wake_up) ? "Y" : "N");
	seq_printf(seq, "\tPMT Magic Frame: %s\n",
		   (priv->dma_cap.pmt_magic_frame) ? "Y" : "N");
	seq_printf(seq, "\tRMON module: %s\n",
		   (priv->dma_cap.rmon) ? "Y" : "N");
	seq_printf(seq, "\tIEEE 1588-2002 Time Stamp: %s\n",
		   (priv->dma_cap.time_stamp) ? "Y" : "N");
	seq_printf(seq, "\tIEEE 1588-2008 Advanced Time Stamp: %s\n",
		   (priv->dma_cap.atime_stamp) ? "Y" : "N");
	seq_printf(seq, "\t802.3az - Energy-Efficient Ethernet (EEE): %s\n",
		   (priv->dma_cap.eee) ? "Y" : "N");
	seq_printf(seq, "\tAV features: %s\n", (priv->dma_cap.av) ? "Y" : "N");
	seq_printf(seq, "\tChecksum Offload in TX: %s\n",
		   (priv->dma_cap.tx_coe) ? "Y" : "N");
	if (priv->synopsys_id >= DWMAC_CORE_4_00) {
		seq_printf(seq, "\tIP Checksum Offload in RX: %s\n",
			   (priv->dma_cap.rx_coe) ? "Y" : "N");
	} else {
		seq_printf(seq, "\tIP Checksum Offload (type1) in RX: %s\n",
			   (priv->dma_cap.rx_coe_type1) ? "Y" : "N");
		seq_printf(seq, "\tIP Checksum Offload (type2) in RX: %s\n",
			   (priv->dma_cap.rx_coe_type2) ? "Y" : "N");
	}
	seq_printf(seq, "\tRXFIFO > 2048bytes: %s\n",
		   (priv->dma_cap.rxfifo_over_2048) ? "Y" : "N");
	seq_printf(seq, "\tNumber of Additional RX channel: %d\n",
		   priv->dma_cap.number_rx_channel);
	seq_printf(seq, "\tNumber of Additional TX channel: %d\n",
		   priv->dma_cap.number_tx_channel);
	seq_printf(seq, "\tNumber of Additional RX queues: %d\n",
		   priv->dma_cap.number_rx_queues);
	seq_printf(seq, "\tNumber of Additional TX queues: %d\n",
		   priv->dma_cap.number_tx_queues);
	seq_printf(seq, "\tEnhanced descriptors: %s\n",
		   (priv->dma_cap.enh_desc) ? "Y" : "N");
	seq_printf(seq, "\tTX Fifo Size: %d\n", priv->dma_cap.tx_fifo_size);
	seq_printf(seq, "\tRX Fifo Size: %d\n", priv->dma_cap.rx_fifo_size);
	seq_printf(seq, "\tHash Table Size: %d\n", priv->dma_cap.hash_tb_sz);
	seq_printf(seq, "\tTSO: %s\n", priv->dma_cap.tsoen ? "Y" : "N");
	seq_printf(seq, "\tNumber of PPS Outputs: %d\n",
		   priv->dma_cap.pps_out_num);
	seq_printf(seq, "\tSafety Features: %s\n",
		   priv->dma_cap.asp ? "Y" : "N");
	seq_printf(seq, "\tFlexible RX Parser: %s\n",
		   priv->dma_cap.frpsel ? "Y" : "N");
	seq_printf(seq, "\tEnhanced Addressing: %d\n",
		   priv->dma_cap.addr64);
	seq_printf(seq, "\tReceive Side Scaling: %s\n",
		   priv->dma_cap.rssen ? "Y" : "N");
	seq_printf(seq, "\tVLAN Hash Filtering: %s\n",
		   priv->dma_cap.vlhash ? "Y" : "N");
	seq_printf(seq, "\tSplit Header: %s\n",
		   priv->dma_cap.sphen ? "Y" : "N");
	seq_printf(seq, "\tVLAN TX Insertion: %s\n",
		   priv->dma_cap.vlins ? "Y" : "N");
	seq_printf(seq, "\tDouble VLAN: %s\n",
		   priv->dma_cap.dvlan ? "Y" : "N");
	seq_printf(seq, "\tNumber of L3/L4 Filters: %d\n",
		   priv->dma_cap.l3l4fnum);
	seq_printf(seq, "\tARP Offloading: %s\n",
		   priv->dma_cap.arpoffsel ? "Y" : "N");
	seq_printf(seq, "\tEnhancements to Scheduled Traffic (EST): %s\n",
		   priv->dma_cap.estsel ? "Y" : "N");
	seq_printf(seq, "\tFrame Preemption (FPE): %s\n",
		   priv->dma_cap.fpesel ? "Y" : "N");
	seq_printf(seq, "\tTime-Based Scheduling (TBS): %s\n",
		   priv->dma_cap.tbssel ? "Y" : "N");
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(tc956xmac_dma_cap);

/* Use network device events to rename debugfs file entries.
 */
static int tc956xmac_device_event(struct notifier_block *unused,
			       unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct tc956xmac_priv *priv = netdev_priv(dev);

	if (dev->netdev_ops != &tc956xmac_netdev_ops)
		goto done;

	switch (event) {
	case NETDEV_CHANGENAME:
		if (priv->dbgfs_dir)
			priv->dbgfs_dir = debugfs_rename(tc956xmac_fs_dir,
							 priv->dbgfs_dir,
							 tc956xmac_fs_dir,
							 dev->name);
		break;
	}
done:
	return NOTIFY_DONE;
}

static struct notifier_block tc956xmac_notifier = {
	.notifier_call = tc956xmac_device_event,
};
#endif
static void tc956xmac_init_fs(struct net_device *dev)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);

	/* Create per netdev entries */
	priv->dbgfs_dir = debugfs_create_dir(dev->name, tc956xmac_fs_dir);

#ifndef TC956X
	/* Entry to report DMA RX/TX rings */
	debugfs_create_file("descriptors_status", 0444, priv->dbgfs_dir, dev,
			    &tc956xmac_rings_status_fops);

	/* Entry to report the DMA HW features */
	debugfs_create_file("dma_cap", 0444, priv->dbgfs_dir, dev,
			    &tc956xmac_dma_cap_fops);

	register_netdevice_notifier(&tc956xmac_notifier);
#endif
}

static void tc956xmac_exit_fs(struct net_device *dev)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
#ifndef TC956X
	unregister_netdevice_notifier(&tc956xmac_notifier);
#endif
	debugfs_remove_recursive(priv->dbgfs_dir);
}
#endif /* CONFIG_DEBUG_FS */

#ifndef TC956X
static u32 tc956xmac_vid_crc32_le(__le16 vid_le)
{
	unsigned char *data = (unsigned char *)&vid_le;
	unsigned char data_byte = 0;
	u32 crc = ~0x0;
	u32 temp = 0;
	int i, bits;

	bits = get_bitmask_order(VLAN_VID_MASK);
	for (i = 0; i < bits; i++) {
		if ((i % 8) == 0)
			data_byte = data[i / 8];

		temp = ((crc & 1) ^ data_byte) & 1;
		crc >>= 1;
		data_byte >>= 1;

		if (temp)
			crc ^= 0xedb88320;
	}

	return crc;
}
#endif

static int tc956xmac_vlan_rx_add_vid(struct net_device *ndev, __be16 proto,
				  u16 vid)
{
	struct tc956xmac_priv *priv = netdev_priv(ndev);
	bool is_double = false;
	int ret = 0;

	if (be16_to_cpu(proto) == ETH_P_8021AD)
		is_double = true;

	tc956xmac_update_vlan_hash(priv, ndev, is_double, vid, PF_DRIVER);

	return ret;
}

static int tc956xmac_vlan_rx_kill_vid(struct net_device *ndev, __be16 proto,
				   u16 vid)
{
	struct tc956xmac_priv *priv = netdev_priv(ndev);
	int ret = 0;

	tc956xmac_delete_vlan(priv, ndev, vid, PF_DRIVER);
	return ret;
}

static const struct net_device_ops tc956xmac_netdev_ops = {
	.ndo_open = tc956xmac_open,
	.ndo_start_xmit = tc956xmac_xmit,
	.ndo_stop = tc956xmac_release,
	.ndo_change_mtu = tc956xmac_change_mtu,
	.ndo_fix_features = tc956xmac_fix_features,
	.ndo_set_features = tc956xmac_set_features,
	.ndo_set_rx_mode = tc956xmac_set_rx_mode,
	.ndo_tx_timeout = tc956xmac_tx_timeout,
	.ndo_do_ioctl = tc956xmac_ioctl,
	.ndo_setup_tc = tc956xmac_setup_tc,
	.ndo_select_queue = tc956xmac_select_queue,
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = tc956xmac_poll_controller,
#endif
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */
	.ndo_set_mac_address = tc956xmac_set_mac_address,
	.ndo_vlan_rx_add_vid = tc956xmac_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid = tc956xmac_vlan_rx_kill_vid,
};

#ifdef TC956X_UNSUPPORTED_UNTESTED
static void tc956xmac_reset_subtask(struct tc956xmac_priv *priv)
{
	if (!test_and_clear_bit(TC956XMAC_RESET_REQUESTED, &priv->state))
		return;
	if (test_bit(TC956XMAC_DOWN, &priv->state))
		return;

	netdev_err(priv->dev, "Reset adapter.\n");

	rtnl_lock();
	netif_trans_update(priv->dev);
	while (test_and_set_bit(TC956XMAC_RESETING, &priv->state))
		usleep_range(1000, 2000);

	set_bit(TC956XMAC_DOWN, &priv->state);
	dev_close(priv->dev);
	dev_open(priv->dev, NULL);
	clear_bit(TC956XMAC_DOWN, &priv->state);
	clear_bit(TC956XMAC_RESETING, &priv->state);
	rtnl_unlock();
}

static void tc956xmac_service_task(struct work_struct *work)
{
	struct tc956xmac_priv *priv = container_of(work, struct tc956xmac_priv,
			service_task);

	tc956xmac_reset_subtask(priv);
	clear_bit(TC956XMAC_SERVICE_SCHED, &priv->state);
}
#endif

/**
 *  tc956xmac_hw_init - Init the MAC device
 *  @priv: driver private structure
 *  Description: this function is to configure the MAC device according to
 *  some platform parameters or the HW capability register. It prepares the
 *  driver to use either ring or chain modes and to setup either enhanced or
 *  normal descriptors.
 */
static int tc956xmac_hw_init(struct tc956xmac_priv *priv)
{
	int ret;

	/* dwmac-sun8i only work in chain mode */
	if (priv->plat->has_sun8i)
		chain_mode = 1;
#ifndef TC956X
	priv->chain_mode = chain_mode;
#endif

	/* Initialize HW Interface */
	ret = tc956xmac_hwif_init(priv);
	if (ret)
		return ret;

	/* Get the HW capability (new GMAC newer than 3.50a) */
	priv->hw_cap_support = tc956xmac_get_hw_features(priv);
	if (priv->hw_cap_support) {
		dev_info(priv->device, "DMA HW capability register supported\n");

		/* We can override some gmac/dma configuration fields: e.g.
		 * enh_desc, tx_coe (e.g. that are passed through the
		 * platform) with the values from the HW capability
		 * register (if supported).
		 */
		priv->plat->enh_desc = priv->dma_cap.enh_desc;
		priv->plat->pmt = priv->dma_cap.pmt_remote_wake_up;
		priv->hw->pmt = priv->plat->pmt;
		if (priv->dma_cap.hash_tb_sz) {
			priv->hw->multicast_filter_bins =
					(BIT(priv->dma_cap.hash_tb_sz) << 5);
			priv->hw->mcast_bits_log2 =
					ilog2(priv->hw->multicast_filter_bins);
		}

		/* TXCOE doesn't work in thresh DMA mode */
		if (priv->plat->force_thresh_dma_mode)
			priv->plat->tx_coe = 0;
		else
			priv->plat->tx_coe = priv->dma_cap.tx_coe;

		/* In case of GMAC4 rx_coe is from HW cap register. */

		priv->plat->rx_coe = priv->dma_cap.rx_coe;

		if (priv->dma_cap.rx_coe_type2)
			priv->plat->rx_coe = TC956XMAC_RX_COE_TYPE2;
		else if (priv->dma_cap.rx_coe_type1)
			priv->plat->rx_coe = TC956XMAC_RX_COE_TYPE1;

	} else {
		dev_info(priv->device, "No HW DMA feature register supported\n");
	}

	if (priv->plat->rx_coe) {
		priv->hw->rx_csum = priv->plat->rx_coe;
		dev_info(priv->device, "RX Checksum Offload Engine supported\n");
		if (priv->synopsys_id < DWMAC_CORE_4_00)
			dev_info(priv->device, "COE Type %d\n", priv->hw->rx_csum);
	}
	if (priv->plat->tx_coe) {
		dev_info(priv->device, "TX Checksum insertion supported\n");
		priv->csum_insertion = 1;	/* Enable Tx Csum Offload */
	}

	if (priv->plat->pmt) {
		dev_info(priv->device, "Wake-Up On Lan supported\n");
		device_set_wakeup_capable(priv->device, 1);
	}

	if (priv->dma_cap.tsoen)
		dev_info(priv->device, "TSO supported\n");

	/* Run HW quirks, if any */
	if (priv->hwif_quirks) {
		ret = priv->hwif_quirks(priv);
		if (ret)
			return ret;
	}

	/* Rx Watchdog is available in the COREs newer than the 3.40.
	 * In some case, for example on bugged HW this feature
	 * has to be disable and this can be done by passing the
	 * riwt_off field from the platform.
	 */
	if (((priv->synopsys_id >= DWMAC_CORE_3_50) ||
	    (priv->plat->has_xgmac)) && (!priv->plat->riwt_off)) {
		priv->use_riwt = 1;
		dev_info(priv->device,
			 "Enable RX Mitigation via HW Watchdog Timer\n");
	}

	return 0;
}

#ifdef TC956X
/*
 * brief This sequence is used to get Tx channel count
 * param[in] count
 * return Success or Failure
 * retval  0 Success
 * retval -1 Failure
 */

static unsigned char tc956x_get_tx_channel_count(struct tc956xmac_resources *res)
{
	unsigned char count;
	unsigned long varMAC_HFR2;
	u32 mac_offset_base = res->port_num == RM_PF0_ID ?
					MAC0_BASE_OFFSET : MAC1_BASE_OFFSET;

	KPRINT_INFO("-->%s\n", __func__);
	varMAC_HFR2 = readl(res->addr + mac_offset_base + XGMAC_HW_FEATURE2_BASE);
	count = ((varMAC_HFR2 & XGMAC_HWFEAT_TXCHCNT) >> XGMAC_HWFEAT_TXCHCNT_SHIFT) + 1;
	KPRINT_INFO("<-- %s\n", __func__);
	return count;
}

/*
 * brief This sequence is used to get Rx channel count
 * param[in] count
 * return Success or Failure
 * retval  0 Success
 * retval -1 Failure
 */

static unsigned char tc956x_get_rx_channel_count(struct tc956xmac_resources *res)
{
	unsigned char count;
	unsigned long varMAC_HFR2;
	u32 mac_offset_base = res->port_num == RM_PF0_ID ?
						MAC0_BASE_OFFSET : MAC1_BASE_OFFSET;

	KPRINT_INFO("--> %s\n", __func__);

	varMAC_HFR2 = readl(res->addr + mac_offset_base + XGMAC_HW_FEATURE2_BASE);
	count = ((varMAC_HFR2 & XGMAC_HWFEAT_RXCHCNT) >> XGMAC_HWFEAT_RXCHCNT_SHIFT) + 1;
	KPRINT_INFO("<-- %s\n", __func__);
	return count;
}
#endif
#ifndef EEPROM_MAC_ADDR

/*!
 * \brief API to validate MAC ID
 *
 * \param[in] char *s - pointer to MAC ID string
 *
 * \return boolean
 *
 * \retval true on success and false on failure.
 */
static bool isMAC(char *s)
{
	int i = 0;

	if (s == NULL)
		return false;

	for (i = 0; i < 17; i++) {
		if (i % 3 != 2 && !isxdigit(s[i]))
			return false;
		if (i % 3 == 2 && s[i] != ':')
			return false;
	}

	return true;
}

/*!
 * \brief API to extract MAC ID from given string
 *
 * \param[in] char *string - pointer to MAC ID string
 *
 * \return None
 */
static void extract_macid(char *string)
{
	char *token_m = NULL;
	int j = 0;
	int mac_id = 0;
	static int addr_found;

	/* Extract MAC ID byte by byte */
	token_m = strsep(&string, ":");

	while (token_m != NULL) {
		sscanf(token_m, "%x", &mac_id);
		if (addr_found < 2) {
			dev_addr[addr_found][j++] = mac_id;
			token_m = strsep(&string, ":");
		} else
			break;
	}
	KPRINT_DEBUG1("MAC Addr : %pM\n", &dev_addr[addr_found][0]);
	addr_found++;
}

/*!
 * \brief API to parse and extract the user configured MAC ID
 *
 * \param[in] file_buf - Pointer to file data buffer
 *
 * \return boolean
 *
 * \return - True on Success and False in failure
 */
static bool lookfor_macid(char *file_buf)
{
	char *string = NULL, *token_n = NULL, *token_s = NULL, *token_m = NULL;
	bool status = false;
	int tc956x_device_no = 0;
	int total_valid_addr = 0;

	string = file_buf;

	/* Parse Line-0 */
	token_n = strsep(&string, "\n");
	while (token_n != NULL) {

		/* Check if line is enabled */
		if (token_n[0] != '#') {
			/* Extract the token based space character */
			token_s = strsep(&token_n, " ");
			if (token_s != NULL) {
				if (strncmp(token_s, config_param_list[0]
					.mdio_key, 9) == 0) {
					token_s = strsep(&token_n, " ");
					token_m = strsep(&token_s, ":");
					sscanf(token_m, "%d",
						&tc956x_device_no);
					if (tc956x_device_no != mdio_bus_id) {
						token_n = strsep(&string, "\n");
						if (token_n == NULL)
							break;
						continue;
					}
				}
			}

			/* Extract the token based space character */
			token_s = strsep(&token_n, " ");
			if (token_s != NULL) {
				/* Compare if parsed string matches with
				 * key listed in configuration table
				 */
				if (strncmp(token_s, config_param_list[0]
					.mac_key, 6) == 0) {

					KPRINT_INFO("MAC_ID Key is found\n");
					/* Read next word */
					token_s = strsep(&token_n, "\n");
					if (token_s != NULL) {

						/* Check if MAC ID length
						 * and MAC ID is valid
						 */
						if ((isMAC(token_s) == true)
							&& (strlen(token_s) ==
							config_param_list[0]
							.mac_str_len)) {
							/* If user configured
							 * MAC ID is valid,
							 * assign default MAC ID
							 */
							extract_macid(token_s);
							total_valid_addr++;

							if (total_valid_addr
								> 1)
								status = true;
						} else {
							KPRINT_INFO
							("Valid Mac not found");
						}
					}
				}
			}
		}
		/* Read next lile */
		token_n = strsep(&string, "\n");

	if (token_n == NULL)
		break;

	}

	return status;
}

/*!
 * \brief Parse the user configuration file for various config
 *
 * \param[in] None
 *
 * \return None
 *
 */
static void parse_config_file(void)
{
	void *data;
	char *cdata;
	int ret, i;
	loff_t size;

	ret = kernel_read_file_from_path("config.ini", &data, &size, 1000, READING_POLICY);

	if (ret < 0) {
		KPRINT_ERR("Mac configuration file not found\n");
		KPRINT_INFO("Using Default MAC Address\n");
		return;
	} else {

		cdata = data;
		/* Parse the file */
		for (i = 0; i < CONFIG_PARAM_NUM; i++) {
			if (strstr((const char *)cdata, config_param_list[i].mdio_key)) {
				KPRINT_DEBUG1("Pattern Match\n");
				if (strncmp(config_param_list[i].mdio_key, "MDIOBUSID", 9) == 0) {
					/* MAC ID Configuration */
					KPRINT_DEBUG1("MAC_ID Configuration\n");
					lookfor_macid(data);
				}
			}
		}
	}

	vfree(data);
	KPRINT_INFO("<--%s", __func__);
}


#endif

/**
 * tc956xmac_dvr_probe
 * @device: device pointer
 * @plat_dat: platform data pointer
 * @res: tc956xmac resource pointer
 * Description: this is the main probe function used to
 * call the alloc_etherdev, allocate the priv structure.
 * Return:
 * returns 0 on success, otherwise errno.
 */
int tc956xmac_dvr_probe(struct device *device,
		     struct plat_tc956xmacenet_data *plat_dat,
		     struct tc956xmac_resources *res)
{
	struct net_device *ndev = NULL;
	struct tc956xmac_priv *priv;
	u32 queue, rxq, maxq;
	int i, ret = 0;
	u8 tx_ch_count, rx_ch_count;
	u32 mac_offset_base = res->port_num == RM_PF0_ID ?
					MAC0_BASE_OFFSET : MAC1_BASE_OFFSET;

#ifdef EEPROM_MAC_ADDR
	u32 mac_addr;
#endif
#ifndef TC956X_WITHOUT_MDIO
	void *nrst_reg, *nclk_reg;
	u32 nrst_val, nclk_val;
#endif
#ifdef TC956X
	KPRINT_INFO("HFR0 Val = 0x%08x", readl(res->addr + mac_offset_base +
							XGMAC_HW_FEATURE0_BASE));
	KPRINT_INFO("HFR1 Val = 0x%08x", readl(res->addr + mac_offset_base +
							XGMAC_HW_FEATURE1_BASE));
	KPRINT_INFO("HFR2 Val = 0x%08x", readl(res->addr + mac_offset_base +
							XGMAC_HW_FEATURE2_BASE));
	KPRINT_INFO("HFR3 Val = 0x%08x", readl(res->addr + mac_offset_base +
							XGMAC_HW_FEATURE3_BASE));


	tx_ch_count = tc956x_get_tx_channel_count(res);
	rx_ch_count = tc956x_get_rx_channel_count(res);

	ndev = devm_alloc_etherdev_mqs(device, sizeof(struct tc956xmac_priv),
				       tx_ch_count, rx_ch_count);
#endif

	if (!ndev) {
		NMSGPR_ALERT(device, "%s:Unable to alloc new net device\n",
				TC956X_RESOURCE_NAME);
		return -ENOMEM;
	}

	SET_NETDEV_DEV(ndev, device);

	priv = netdev_priv(ndev);
	priv->device = device;
	priv->dev = ndev;
	priv->ioaddr = res->addr;

	ret = tc956x_platform_probe(priv, res);
	if (ret) {
		dev_err(priv->device, "Platform probe error %d\n", ret);
		return -EPERM;
	}
#ifdef TC956X
	priv->mac_loopback_mode = 0; /* Disable MAC loopback by default */
	priv->phy_loopback_mode = 0; /* Disable PHY loopback by default */
	priv->tc956x_port_pm_suspend = false; /* By Default Port suspend state set to false */
#endif
	/* ToDo : Firwmware load code here */

	tc956xmac_set_ethtool_ops(ndev);
	priv->pause = pause;
	priv->plat = plat_dat;
#ifdef TC956X
	priv->tc956x_SFR_pci_base_addr = res->tc956x_SFR_pci_base_addr;
	priv->tc956x_SRAM_pci_base_addr = res->tc956x_SRAM_pci_base_addr;
	priv->tc956x_BRIDGE_CFG_pci_base_addr = res->tc956x_BRIDGE_CFG_pci_base_addr;
#endif
	priv->port_num = res->port_num;
	priv->dev->base_addr = (unsigned long)res->addr;
	if (priv->plat->interface == PHY_INTERFACE_MODE_SGMII)
		priv->is_sgmii_2p5g = true;
	else
		priv->is_sgmii_2p5g = false;
	priv->dev->irq = res->irq;
	priv->wol_irq = res->wol_irq;
	priv->lpi_irq = res->lpi_irq;
	priv->port_interface = res->port_interface;
	priv->eee_enabled = res->eee_enabled;
	priv->tx_lpi_timer = res->tx_lpi_timer;

#ifdef DMA_OFFLOAD_ENABLE
	priv->client_priv = NULL;
	memset(priv->cm3_tamap, 0, sizeof(struct tc956xmac_cm3_tamap) * MAX_CM3_TAMAP_ENTRIES);
#endif

#ifdef TC956X
	/* Read mac address from config.ini file */
	++mdio_bus_id;

#ifdef EEPROM_MAC_ADDR

#ifdef TC956X
	mac_addr = readl(priv->tc956x_SRAM_pci_base_addr +
			TC956X_M3_SRAM_EEPROM_MAC_ADDR + (priv->port_num * 8));

	if (mac_addr != 0) {
		dev_addr[priv->port_num][0] = (mac_addr & 0x000000FF);
		dev_addr[priv->port_num][1] = (mac_addr & 0x0000FF00) >> 8;
		dev_addr[priv->port_num][2] = (mac_addr & 0x00FF0000) >> 16;
		dev_addr[priv->port_num][3] = (mac_addr & 0xFF000000) >> 24;

		mac_addr = readl(priv->tc956x_SRAM_pci_base_addr +
				TC956X_M3_SRAM_EEPROM_MAC_ADDR + 0x4 + (priv->port_num * 8));
		dev_addr[priv->port_num][4] = (mac_addr & 0x000000FF);
		dev_addr[priv->port_num][5] = (mac_addr & 0x0000FF00) >> 8;
	}
#endif

#else
	/* To be enabled for config.ini parsing */
	parse_config_file();

#endif /* EEPROM_MAC_ADDR */

	res->mac = &dev_addr[priv->port_num][0];

	NMSGPR_INFO(device, "MAC Address: %02x:%02x:%02x:%02x:%02x:%02x\n",
		dev_addr[priv->port_num][0], dev_addr[priv->port_num][1],
		dev_addr[priv->port_num][2], dev_addr[priv->port_num][3],
		dev_addr[priv->port_num][4], dev_addr[priv->port_num][5]);

#endif
	if (!IS_ERR_OR_NULL(res->mac))
		memcpy(priv->dev->dev_addr, res->mac, ETH_ALEN);

	dev_set_drvdata(device, priv->dev);

	/* Verify driver arguments */
	tc956xmac_verify_args();

#ifdef TC956X_UNSUPPORTED_UNTESTED
	/* Allocate workqueue */
	priv->wq = create_singlethread_workqueue("tc956xmac_wq");
	if (!priv->wq) {
		dev_err(priv->device, "failed to create workqueue\n");
		return -ENOMEM;
	}

	INIT_WORK(&priv->service_task, tc956xmac_service_task);
#endif

	/* Override with kernel parameters if supplied XXX CRS XXX
	 * this needs to have multiple instances
	 */
	if ((phyaddr >= 0) && (phyaddr <= 31))
		priv->plat->phy_addr = phyaddr;

	if (priv->plat->tc956xmac_rst) {
		ret = reset_control_assert(priv->plat->tc956xmac_rst);
		reset_control_deassert(priv->plat->tc956xmac_rst);
		/* Some reset controllers have only reset callback instead of
		 * assert + deassert callbacks pair.
		 */
		if (ret == -ENOTSUPP)
			reset_control_reset(priv->plat->tc956xmac_rst);
	}

	/* Init MAC and get the capabilities */
	ret = tc956xmac_hw_init(priv);
	if (ret)
		goto error_hw_init;

	tc956xmac_check_ether_addr(priv);

	/* Configure real RX and TX queues */
	netif_set_real_num_rx_queues(ndev, priv->plat->rx_queues_to_use);
	netif_set_real_num_tx_queues(ndev, priv->plat->tx_queues_to_use);

	ndev->netdev_ops = &tc956xmac_netdev_ops;

	ndev->hw_features = NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
				NETIF_F_RXCSUM;

	ret = tc956xmac_tc_init(priv, NULL);
	if (!ret)
		ndev->hw_features |= NETIF_F_HW_TC;

	/* Enable TSO module if any Queue TSO is Enabled */
	for (queue = 0; queue < MTL_MAX_TX_QUEUES; queue++) {
		if (priv->plat->tx_queues_cfg[0].tso_en == TC956X_ENABLE)
			priv->plat->tso_en = 1;
	}

	if ((priv->plat->tso_en) && (priv->dma_cap.tsoen)) {
		ndev->hw_features |= NETIF_F_TSO | NETIF_F_TSO6;
		if (priv->plat->has_gmac4)
			ndev->hw_features |= NETIF_F_GSO_UDP_L4;
		priv->tso = true;
		dev_info(priv->device, "TSO feature enabled\n");
	} else {
		priv->tso = false;
	}

	if (priv->dma_cap.sphen && priv->plat->sph_en) {
		ndev->hw_features |= NETIF_F_GRO;
		priv->sph = true;
		dev_info(priv->device, "SPH feature enabled\n");
	}

	if (priv->dma_cap.addr64) {
		ret = dma_set_mask_and_coherent(device,
				DMA_BIT_MASK(priv->dma_cap.addr64));
		if (!ret) {
			dev_info(priv->device, "Using %d bits DMA width\n",
				 priv->dma_cap.addr64);

		} else {
			ret = dma_set_mask_and_coherent(device,
					DMA_BIT_MASK(32));
			if (ret) {
				dev_err(priv->device, "Failed to set\
						DMA Mask\n");
				goto error_hw_init;
			}

			priv->dma_cap.addr64 = 32;
		}
	}
	/*
	 * Enable enhanced addressing mode in all configurations to support
	 * mapping 0x10 XXXX XXXX TC956X address space to host.
	 */
	priv->plat->dma_cfg->eame = true;

	ndev->features |= ndev->hw_features | NETIF_F_HIGHDMA;

	/* Ethtool rx-fcs state is Off by default */
	ndev->hw_features |= NETIF_F_RXFCS;

	ndev->watchdog_timeo = msecs_to_jiffies(watchdog);
#ifdef TC956XMAC_VLAN_TAG_USED
	/* Both mac100 and gmac support receive VLAN tag detection */
	/* Driver only supports CTAG */
	ndev->features &= ~NETIF_F_HW_VLAN_CTAG_RX; /* Disable rx-vlan-filter by default */
	ndev->hw_features |= NETIF_F_HW_VLAN_CTAG_RX; /* Support as User-changeable features */
#ifndef TC956X
	ndev->features |= NETIF_F_HW_VLAN_STAG_RX;
#endif
	if (priv->dma_cap.vlhash) {
		/* Driver only supports CTAG */
		ndev->features &= ~NETIF_F_HW_VLAN_CTAG_FILTER; /* Disable rx-vlan-offload by default */
		ndev->hw_features |= NETIF_F_HW_VLAN_CTAG_FILTER; /* Support as User-changeable features */
#ifndef TC956X
		ndev->features |= NETIF_F_HW_VLAN_STAG_FILTER;
#endif
	}
	if (priv->dma_cap.vlins) {
		ndev->features |= NETIF_F_HW_VLAN_CTAG_TX; /* Enable tx-vlan-offload by default */
		ndev->hw_features |= NETIF_F_HW_VLAN_CTAG_TX; /* Support as User-changeable features */
#ifndef TC956X
		if (priv->dma_cap.dvlan)
			ndev->features |= NETIF_F_HW_VLAN_STAG_TX;
#endif
	}
#endif
	priv->mac_table =
		kcalloc(TC956X_MAX_PERFECT_ADDRESSES - XGMAC_ADDR_ADD_SKIP_OFST,
			sizeof(struct tc956x_mac_addr), GFP_KERNEL);

	if (!priv->mac_table)
		goto error_hw_init;

	priv->vlan_table =
		kcalloc(TC956X_MAX_PERFECT_VLAN, sizeof(struct tc956x_vlan_id), GFP_KERNEL);

	if (!priv->vlan_table) {
		kfree(priv->mac_table);
		goto error_hw_init;
	}
	priv->msg_enable = netif_msg_init(debug, default_msg_level);

	/* Initialize RSS */
	rxq = priv->plat->rx_queues_to_use;
	netdev_rss_key_fill(priv->rss.key, sizeof(priv->rss.key));
	for (i = 0; i < ARRAY_SIZE(priv->rss.table); i++)
		priv->rss.table[i] = ethtool_rxfh_indir_default(i, rxq);

	if (priv->dma_cap.rssen && priv->plat->rss_en)
		ndev->features |= NETIF_F_RXHASH;

	/* MTU range: 46 - hw-specific max */
	ndev->min_mtu = ETH_ZLEN - ETH_HLEN;
	if (priv->plat->has_xgmac)
		ndev->max_mtu = XGMAC_JUMBO_LEN;
	else if ((priv->plat->enh_desc) || (priv->synopsys_id >= DWMAC_CORE_4_00))
		ndev->max_mtu = JUMBO_LEN;
	else
		ndev->max_mtu = SKB_MAX_HEAD(NET_SKB_PAD + NET_IP_ALIGN);
	/* Will not overwrite ndev->max_mtu if plat->maxmtu > ndev->max_mtu
	 * as well as plat->maxmtu < ndev->min_mtu which is a invalid range.
	 */
	if ((priv->plat->maxmtu < ndev->max_mtu) &&
	    (priv->plat->maxmtu >= ndev->min_mtu))
		ndev->max_mtu = priv->plat->maxmtu;
	else if (priv->plat->maxmtu < ndev->min_mtu)
		dev_warn(priv->device,
			 "%s: warning: maxmtu having invalid value (%d)\n",
			 __func__, priv->plat->maxmtu);

	netdev_dbg(priv->dev, "%s: MAX MTU size = %d", __func__, ndev->max_mtu);

	if (flow_ctrl)
		priv->flow_ctrl = FLOW_AUTO;/* RX/TX pause on */

	/* Setup channels NAPI */
	maxq = max(priv->plat->rx_queues_to_use, priv->plat->tx_queues_to_use);

	for (queue = 0; queue < maxq; queue++) {
		struct tc956xmac_channel *ch = &priv->channel[queue];

		spin_lock_init(&ch->lock);
		ch->priv_data = priv;
		ch->index = queue;

		if ((queue < priv->plat->rx_queues_to_use) &&
			(priv->plat->rx_dma_ch_owner[queue] == USE_IN_TC956X_SW)) {
			netif_napi_add(ndev, &ch->rx_napi, tc956xmac_napi_poll_rx,
				       NAPI_POLL_WEIGHT);
		}
		if ((queue < priv->plat->tx_queues_to_use) &&
			(priv->plat->tx_dma_ch_owner[queue] == USE_IN_TC956X_SW)) {
			netif_tx_napi_add(ndev, &ch->tx_napi,
					  tc956xmac_napi_poll_tx,
					  NAPI_POLL_WEIGHT);
		}
	}

	mutex_init(&priv->lock);

	/* If a specific clk_csr value is passed from the platform
	 * this means that the CSR Clock Range selection cannot be
	 * changed at run-time and it is fixed. Viceversa the driver'll try to
	 * set the MDC clock dynamically according to the csr actual
	 * clock input.
	 */
	if (priv->plat->clk_csr >= 0)
		priv->clk_csr = priv->plat->clk_csr;
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
	else
		tc956xmac_clk_csr_set(priv);
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */

	tc956xmac_check_pcs_mode(priv);

#ifdef TC956X
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

#endif
	if (priv->hw->pcs != TC956XMAC_PCS_RGMII  &&
		priv->hw->pcs != TC956XMAC_PCS_TBI &&
		priv->hw->pcs != TC956XMAC_PCS_RTBI) {
		/* MDIO bus Registration */
#ifdef TC956X_WITHOUT_MDIO
		if (priv->dma_cap.sma_mdio == 1) {
#endif
			ret = tc956xmac_mdio_register(ndev);
			if (ret < 0) {
				/* tc956xmac_mdio_register() will return -ENODEV when No PHY is found */
				if (ret == -ENODEV) {
					dev_info(priv->device, "Port%d will not be registered as ethernet controller", priv->port_num);
					goto error_mdio_register;
				} else {
					dev_err(priv->device,
					"%s: MDIO bus (id: %d) registration failed",
					__func__, priv->plat->bus_id);
					goto error_mdio_register;
				}
		}
	}
#ifdef TC956X_WITHOUT_MDIO
	}
#endif
	ret = tc956xmac_phy_setup(priv);
	if (ret) {
		netdev_err(ndev, "failed to setup phy (%d)\n", ret);
		goto error_phy_setup;
	}

	ret = register_netdev(ndev);
	if (ret) {
		dev_err(priv->device, "%s: ERROR %i registering the device\n",
			__func__, ret);
		goto error_netdev_register;
	}

#ifdef CONFIG_DEBUG_FS
	tc956xmac_init_fs(ndev);
#endif

	return ret;

error_netdev_register:
	phylink_destroy(priv->phylink);
error_phy_setup:
	if (priv->hw->pcs != TC956XMAC_PCS_RGMII &&
		priv->hw->pcs != TC956XMAC_PCS_TBI &&
		priv->hw->pcs != TC956XMAC_PCS_RTBI)
		tc956xmac_mdio_unregister(ndev);
error_mdio_register:
	for (queue = 0; queue < maxq; queue++) {
		struct tc956xmac_channel *ch = &priv->channel[queue];

		if (queue < priv->plat->rx_queues_to_use &&
				priv->plat->rx_dma_ch_owner[queue] == USE_IN_TC956X_SW)
			netif_napi_del(&ch->rx_napi);
		if (queue < priv->plat->tx_queues_to_use &&
				priv->plat->tx_dma_ch_owner[queue] == USE_IN_TC956X_SW)
			netif_napi_del(&ch->tx_napi);
	}
#ifndef TC956X_WITHOUT_MDIO
	if (priv->port_num == 0) {
		nrst_reg = priv->tc956x_SFR_pci_base_addr + NRSTCTRL0_OFFSET;
		nclk_reg = priv->tc956x_SFR_pci_base_addr + NCLKCTRL0_OFFSET;
	} else {
		nrst_reg = priv->tc956x_SFR_pci_base_addr + NRSTCTRL1_OFFSET;
		nclk_reg = priv->tc956x_SFR_pci_base_addr + NCLKCTRL1_OFFSET;
	}
	nrst_val = readl(nrst_reg);
	nclk_val = readl(nclk_reg);
	KPRINT_INFO("%s : Port %d Rd RST Reg:%x, CLK Reg:%x", __func__, priv->port_num,
		nrst_val, nclk_val);
	/* Assert reset and Disable Clock for EMAC */
	nrst_val = nrst_val | NRSTCTRL_EMAC_MASK;
	nclk_val = nclk_val & ~NCLKCTRL_EMAC_MASK;
	writel(nrst_val, nrst_reg);
	writel(nclk_val, nclk_reg);
	KPRINT_INFO("%s : Port %d Wr RST Reg:%x, CLK Reg:%x", __func__, priv->port_num,
		readl(nrst_reg), readl(nclk_reg));
#endif
error_hw_init:
#ifndef TC956X
	destroy_workqueue(priv->wq);
#endif

	return ret;
}
EXPORT_SYMBOL_GPL(tc956xmac_dvr_probe);

/**
 * tc956xmac_dvr_remove
 * @dev: device pointer
 * Description: this function resets the TX/RX processes, disables the MAC RX/TX
 * changes the link status, releases the DMA descriptor rings.
 */
int tc956xmac_dvr_remove(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct tc956xmac_priv *priv = netdev_priv(ndev);
	u32 val;
	struct phy_device *phydev;
	int addr = priv->plat->phy_addr;

	netdev_info(priv->dev, "%s: removing driver", __func__);

	phydev = mdiobus_get_phy(priv->mii, addr);

	if(phydev->drv != NULL) {
		if ((true == priv->plat->phy_interrupt_mode) && (phydev->drv->config_intr))
			cancel_work_sync(&priv->emac_phy_work);
	}

#ifdef CONFIG_DEBUG_FS
	tc956xmac_exit_fs(ndev);
#endif
	tc956xmac_stop_all_dma(priv);

	if (tc956x_platform_remove(priv)) {
		dev_err(priv->device, "Platform remove error\n");
	}
	tc956xmac_mac_set(priv, priv->ioaddr, false);
	netif_carrier_off(ndev);
	unregister_netdev(ndev);
	phylink_destroy(priv->phylink);

	kfree(priv->mac_table);
	kfree(priv->vlan_table);

	if (priv->plat->tc956xmac_rst)
		reset_control_assert(priv->plat->tc956xmac_rst);
	clk_disable_unprepare(priv->plat->pclk);
	clk_disable_unprepare(priv->plat->tc956xmac_clk);
	if (priv->hw->pcs != TC956XMAC_PCS_RGMII &&
		priv->hw->pcs != TC956XMAC_PCS_TBI &&
		priv->hw->pcs != TC956XMAC_PCS_RTBI)
		tc956xmac_mdio_unregister(ndev);
#ifndef TC956X
	destroy_workqueue(priv->wq);
#endif
	mutex_destroy(&priv->lock);
#ifdef TC956X
	val = ioread32((void __iomem *)(priv->tc956x_SRAM_pci_base_addr + TC956X_M3_FW_EXIT));
	val += 1;
#ifdef DISABLE_EMAC_PORT1
	val = TC956X_M3_FW_EXIT_VALUE;
#endif
	iowrite32(val, (void __iomem *)(priv->tc956x_SRAM_pci_base_addr + TC956X_M3_FW_EXIT));
#endif

	return 0;
}
EXPORT_SYMBOL_GPL(tc956xmac_dvr_remove);

/**
 * tc956xmac_suspend - suspend callback
 * @dev: device pointer
 * Description: this is the function to suspend the device and it is called
 * by the platform driver to stop the network queue, release the resources,
 * program the PMT register (for WoL), clean and release driver resources.
 */
int tc956xmac_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct tc956xmac_priv *priv = netdev_priv(ndev);
	struct phy_device *phydev = NULL; /* For cancelling Work queue */
	int addr = priv->plat->phy_addr;

	KPRINT_INFO("---> %s : Port %d", __func__, priv->port_num);
	if ((priv->plat->phy_addr != -1) && (priv->mii != NULL))
		phydev = mdiobus_get_phy(priv->mii, addr);

	if (!ndev)
		return 0;

	if (!phydev) {
		DBGPR_FUNC(priv->device, "%s Error : No phy at Addr %d or MDIO Unavailable \n", 
			__func__, addr);
		return 0;
	}

	/* Disabling EEE for issue in TC9560/62, to be tested for TC956X */
	if (priv->eee_enabled)
		tc956xmac_disable_eee_mode(priv);

	//if (priv->wolopts) {
	//	KPRINT_INFO("%s : Port %d - Phy Speed Down", __func__, priv->port_num);
	//	phy_speed_down(phydev, true);
	//}

	if (!netif_running(ndev))
		goto clean_exit;

	/* Cancel all work-queues before suspend start only when net interface is up and running */
	if (phydev->drv != NULL) {
		if ((true == priv->plat->phy_interrupt_mode) && 
		(phydev->drv->config_intr)) {
			DBGPR_FUNC(priv->device, "%s : (Flush All PHY work-queues) \n", __func__);
			cancel_work_sync(&priv->emac_phy_work);
		}
	}

	/* Invoke device driver close only when net inteface is up and running. */
	rtnl_lock();
	tc956xmac_release(ndev);
	rtnl_unlock();

clean_exit:
	/* Detach network device */
	netif_device_detach(ndev);

	priv->oldlink = false;
	priv->speed = SPEED_UNKNOWN;
	priv->oldduplex = DUPLEX_UNKNOWN;

	KPRINT_INFO("<--- %s : Port %d", __func__, priv->port_num);
	return 0;
}
EXPORT_SYMBOL_GPL(tc956xmac_suspend);
#ifndef TC956X
/**
 * tc956xmac_reset_queues_param - reset queue parameters
 * @dev: device pointer
 */
static void tc956xmac_reset_queues_param(struct tc956xmac_priv *priv)
{
	u32 rx_cnt = priv->plat->rx_queues_to_use;
	u32 tx_cnt = priv->plat->tx_queues_to_use;
	u32 queue;

	for (queue = 0; queue < rx_cnt; queue++) {
		struct tc956xmac_rx_queue *rx_q = &priv->rx_queue[queue];

		if (priv->plat->rx_dma_ch_owner[queue] != USE_IN_TC956X_SW)
			continue;

		rx_q->cur_rx = 0;
		rx_q->dirty_rx = 0;
	}

	for (queue = 0; queue < tx_cnt; queue++) {
		struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[queue];

		if (priv->plat->tx_dma_ch_owner[queue] != USE_IN_TC956X_SW)
			continue;

		tx_q->cur_tx = 0;
		tx_q->dirty_tx = 0;
		tx_q->mss = 0;
	}
}
#endif

/**
 * tc956xmac_resume - resume callback
 * @dev: device pointer
 * Description: when resume this function is invoked to setup the DMA and CORE
 * in a usable state.
 */
int tc956xmac_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct tc956xmac_priv *priv = netdev_priv(ndev);
	struct tc956xmac_resources res;
	u32 cm3_reset_status = 0;
	s32 fw_load_status = 0;

	KPRINT_INFO("---> %s : Port %d", __func__, priv->port_num);

	memset(&res, 0, sizeof(res));
	cm3_reset_status = readl((priv->ioaddr + NRSTCTRL0_OFFSET));
	if ((cm3_reset_status & NRSTCTRL0_MCURST) == NRSTCTRL0_MCURST) {
		res.tc956x_SFR_pci_base_addr = priv->tc956x_SFR_pci_base_addr;
		res.addr = priv->ioaddr;
		res.tc956x_SRAM_pci_base_addr = priv->tc956x_SRAM_pci_base_addr;
		res.irq = priv->dev->irq;
		fw_load_status = tc956x_load_firmware(dev, &res);
		if (fw_load_status < 0) {
			KPRINT_ERR("Firmware load failed\n");
			return -EINVAL;
		}
	}

	//if (priv->wolopts) {
	//	KPRINT_INFO("%s : Port %d - Phy Speed Up", __func__, priv->port_num);
	//	phy_speed_up(phydev);
	//}
#ifndef TC956X
	/* Reset Parameters. */
	tc956xmac_reset_queues_param(priv);
#endif
	if (!netif_running(ndev))
		goto clean_exit;
	/* Invoke device driver open */
	rtnl_lock();
	tc956xmac_open(ndev);
	rtnl_unlock();

	/* Attach network device */
	netif_device_attach(ndev);

clean_exit:
	if (priv->tc956xmac_pm_wol_interrupt) {
		KPRINT_INFO("%s : Port %d Clearing WOL and queuing phy work", __func__, priv->port_num);
		/* Clear WOL Interrupt after resume, if WOL enabled */
		priv->tc956xmac_pm_wol_interrupt = false;
		/* Queue the work in system_wq */
		queue_work(system_wq, &priv->emac_phy_work);
	}
	KPRINT_INFO("<--- %s : Port %d", __func__, priv->port_num);
	return 0;
}
EXPORT_SYMBOL_GPL(tc956xmac_resume);

#ifndef MODULE
static int __init tc956xmac_cmdline_opt(char *str)
{
	char *opt;

	if (!str || !*str)
		return -EINVAL;
	while ((opt = strsep(&str, ",")) != NULL) {
		if (!strncmp(opt, "debug:", 6)) {
			if (kstrtoint(opt + 6, 0, &debug))
				goto err;
		} else if (!strncmp(opt, "phyaddr:", 8)) {
			if (kstrtoint(opt + 8, 0, &phyaddr))
				goto err;
		} else if (!strncmp(opt, "buf_sz:", 7)) {
			if (kstrtoint(opt + 7, 0, &buf_sz))
				goto err;
		} else if (!strncmp(opt, "tc:", 3)) {
			if (kstrtoint(opt + 3, 0, &tc))
				goto err;
		} else if (!strncmp(opt, "watchdog:", 9)) {
			if (kstrtoint(opt + 9, 0, &watchdog))
				goto err;
		} else if (!strncmp(opt, "flow_ctrl:", 10)) {
			if (kstrtoint(opt + 10, 0, &flow_ctrl))
				goto err;
		} else if (!strncmp(opt, "pause:", 6)) {
			if (kstrtoint(opt + 6, 0, &pause))
				goto err;
		} else if (!strncmp(opt, "eee_timer:", 10)) {
			if (kstrtoint(opt + 10, 0, &eee_timer))
				goto err;
		} else if (!strncmp(opt, "chain_mode:", 11)) {
			if (kstrtoint(opt + 11, 0, &chain_mode))
				goto err;
		}
	}
	return 0;

err:
	pr_err("%s: ERROR broken module parameter conversion", __func__);
	return -EINVAL;
}

__setup("tc956xmaceth=", tc956xmac_cmdline_opt);
#endif /* MODULE */

#ifdef TC956X
int tc956xmac_init(void)
{
#ifdef CONFIG_DEBUG_FS
	/* Create debugfs main directory if it doesn't exist yet */
	if (!tc956xmac_fs_dir)
		tc956xmac_fs_dir = debugfs_create_dir(TC956X_RESOURCE_NAME, NULL);
#endif

	return 0;
}

void tc956xmac_exit(void)
{
#ifdef CONFIG_DEBUG_FS
	debugfs_remove_recursive(tc956xmac_fs_dir);
#endif
}
#endif

#ifndef TC956X
MODULE_DESCRIPTION("TC956X PCI Express Ethernet Network Driver");
MODULE_AUTHOR("Toshiba Electronic Devices & Storage Corporation");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_MODULE_VERSION);
#endif
