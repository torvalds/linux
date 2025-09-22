/*	$OpenBSD: if_urereg.h,v 1.14 2025/05/23 03:06:09 kevlo Exp $	*/
/*-
 * Copyright (c) 2015, 2016, 2019 Kevin Lo <kevlo@openbsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#define	URE_CONFIG_IDX		0	/* config number 1 */
#define	URE_IFACE_IDX		0

#define	URE_CTL_READ		0x01
#define	URE_CTL_WRITE		0x02

#define	URE_TIMEOUT		1000
#define	URE_PHY_TIMEOUT		2000

#define	URE_BYTE_EN_DWORD	0xff
#define	URE_BYTE_EN_WORD	0x33
#define	URE_BYTE_EN_BYTE	0x11
#define	URE_BYTE_EN_SIX_BYTES	0x3f

#define URE_FRAMELEN(mtu)	\
	(mtu + ETHER_HDR_LEN + ETHER_CRC_LEN + ETHER_VLAN_ENCAP_LEN)
#define URE_JUMBO_FRAMELEN	(9 * 1024)
#define URE_JUMBO_MTU						\
	(URE_JUMBO_FRAMELEN - ETHER_HDR_LEN - ETHER_CRC_LEN -	\
	 ETHER_VLAN_ENCAP_LEN)

#define	URE_PLA_IDR		0xc000
#define	URE_PLA_RCR		0xc010
#define	URE_PLA_RCR1		0xc012
#define	URE_PLA_RMS		0xc016
#define	URE_PLA_RXFIFO_CTRL0	0xc0a0
#define	URE_PLA_RXFIFO_FULL	0xc0a2
#define	URE_PLA_RXFIFO_CTRL1	0xc0a4
#define	URE_PLA_RX_FIFO_FULL	0xc0a6
#define	URE_PLA_RXFIFO_CTRL2	0xc0a8
#define	URE_PLA_RX_FIFO_EMPTY	0xc0aa
#define	URE_PLA_DMY_REG0	0xc0b0
#define	URE_PLA_FMC		0xc0b4
#define	URE_PLA_CFG_WOL		0xc0b6
#define	URE_PLA_TEREDO_CFG	0xc0bc
#define	URE_PLA_MAR		0xcd00
#define	URE_PLA_BACKUP		0xd000
#define	URE_PLA_BDC_CR		0xd1a0
#define	URE_PLA_TEREDO_TIMER	0xd2cc
#define	URE_PLA_REALWOW_TIMER	0xd2e8
#define	URE_PLA_SUSPEND_FLAG	0xd38a
#define	URE_PLA_INDICATE_FALG	0xd38c
#define	URE_PLA_EXTRA_STATUS	0xd398
#define	URE_PLA_GPHY_CTRL	0xd3ae
#define	URE_PLA_POL_GPIO_CTRL	0xdc6a
#define	URE_PLA_LEDSEL		0xdd90
#define	URE_PLA_LED_FEATURE	0xdd92
#define	URE_PLA_PHYAR		0xde00
#define	URE_PLA_BOOT_CTRL	0xe004
#define	URE_PLA_LWAKE_CTRL_REG	0xe007
#define	URE_PLA_GPHY_INTR_IMR	0xe022
#define	URE_PLA_EEE_CR		0xe040
#define	URE_PLA_EEEP_CR		0xe080
#define	URE_PLA_MAC_PWR_CTRL	0xe0c0
#define	URE_PLA_MAC_PWR_CTRL2	0xe0ca
#define	URE_PLA_MAC_PWR_CTRL3	0xe0cc
#define	URE_PLA_MAC_PWR_CTRL4	0xe0ce
#define	URE_PLA_WDT6_CTRL	0xe428
#define	URE_PLA_TCR0		0xe610
#define	URE_PLA_TCR1		0xe612
#define	URE_PLA_MTPS		0xe615
#define	URE_PLA_TXFIFO_CTRL	0xe618
#define	URE_PLA_TXFIFO_FULL	0xe61a
#define	URE_PLA_RSTTALLY	0xe800
#define	URE_PLA_CR		0xe813
#define	URE_PLA_CRWECR		0xe81c
#define	URE_PLA_CONFIG34	0xe820
#define	URE_PLA_CONFIG5		0xe822
#define	URE_PLA_PHY_PWR		0xe84c
#define	URE_PLA_OOB_CTRL	0xe84f
#define	URE_PLA_CPCR		0xe854
#define	URE_PLA_MISC_0		0xe858
#define	URE_PLA_MISC_1		0xe85a
#define	URE_PLA_OCP_GPHY_BASE	0xe86c
#define	URE_PLA_TELLYCNT	0xe890
#define	URE_PLA_SFF_STS_7	0xe8de
#define	URE_PLA_PHYSTATUS	0xe908
#define	URE_PLA_CONFIG6		0xe90a
#define	URE_PLA_USB_CFG		0xe952

#define	URE_USB_USB2PHY		0xb41e
#define	URE_USB_SSPHYLINK1	0xb426
#define	URE_USB_SSPHYLINK2	0xb428
#define	URE_USB_L1_CTRL		0xb45e
#define	URE_USB_U2P3_CTRL	0xb460
#define	URE_USB_CSR_DUMMY1	0xb464
#define	URE_USB_CSR_DUMMY2	0xb466
#define	URE_USB_DEV_STAT	0xb808
#define	URE_USB_U2P3_CTRL2	0xc2c0
#define	URE_USB_CONNECT_TIMER	0xcbf8
#define	URE_USB_MSC_TIMER	0xcbfc
#define	URE_USB_BURST_SIZE	0xcfc0
#define	URE_USB_LPM_CONFIG	0xcfd8
#define	URE_USB_ECM_OPTION	0xcfee
#define	URE_USB_MISC_2		0xcfff
#define	URE_USB_ECM_OP		0xd26b
#define	URE_USB_GPHY_CTRL	0xd284
#define	URE_USB_SPEED_OPTION	0xd32a
#define	URE_USB_FW_CTRL		0xd334
#define	URE_USB_FC_TIMER	0xd340
#define	URE_USB_USB_CTRL	0xd406
#define	URE_USB_PHY_CTRL	0xd408
#define	URE_USB_TX_AGG		0xd40a
#define	URE_USB_RX_BUF_TH	0xd40c
#define	URE_USB_LPM_CTRL	0xd41a
#define	URE_USB_USB_TIMER	0xd428
#define	URE_USB_RX_EARLY_AGG	0xd42c
#define	URE_USB_RX_EARLY_SIZE	0xd42e
#define	URE_USB_PM_CTRL_STATUS	0xd432
#define	URE_USB_TX_DMA		0xd434
#define	URE_USB_UPT_RXDMA_OWN	0xd437
#define	URE_USB_TOLERANCE	0xd490
#define	URE_USB_BMU_RESET	0xd4b0
#define	URE_USB_BMU_CONFIG	0xd4b4
#define	URE_USB_U1U2_TIMER	0xd4da
#define	URE_USB_FW_TASK		0xd4e8
#define	URE_USB_RX_AGGR_NUM	0xd4ee
#define	URE_USB_CMD_ADDR	0xd5d6
#define	URE_USB_CMD_DATA	0xd5d8
#define	URE_USB_CMD		0xd5dc
#define	URE_USB_TGPHY_ADDR	0xd630
#define	URE_USB_TGPHY_DATA	0xd632
#define	URE_USB_TGPHY_CMD	0xd634
#define	URE_USB_UPS_CTRL	0xd800
#define	URE_USB_POWER_CUT	0xd80a
#define	URE_USB_MISC_0		0xd81a
#define	URE_USB_AFE_CTRL2	0xd824
#define	URE_USB_UPS_FLAGS	0xd848
#define	URE_USB_WDT11_CTRL	0xe43c

/* OCP Registers. */
#define	URE_OCP_ALDPS_CONFIG	0x2010
#define	URE_OCP_EEE_CONFIG1	0x2080
#define	URE_OCP_EEE_CONFIG2	0x2092
#define	URE_OCP_EEE_CONFIG3	0x2094
#define	URE_OCP_BASE_MII	0xa400
#define	URE_OCP_EEE_AR		0xa41a
#define	URE_OCP_EEE_DATA	0xa41c
#define	URE_OCP_PHY_STATUS	0xa420
#define	URE_OCP_POWER_CFG	0xa430
#define	URE_OCP_EEE_CFG		0xa432
#define	URE_OCP_SRAM_ADDR	0xa436
#define	URE_OCP_SRAM_DATA	0xa438
#define	URE_OCP_DOWN_SPEED	0xa442
#define	URE_OCP_EEE_ABLE	0xa5c4
#define	URE_OCP_EEE_ADV		0xa5d0
#define	URE_OCP_EEE_LPABLE	0xa5d2
#define	URE_OCP_10GBT_CTRL	0xa5d4
#define	URE_OCP_PHY_STATE	0xa708
#define	URE_OCP_ADC_CFG		0xbc06

/* SRAM Register. */
#define	URE_SRAM_LPF_CFG	0x8012
#define	URE_SRAM_10M_AMP1	0x8080
#define	URE_SRAM_10M_AMP2	0x8082
#define	URE_SRAM_IMPEDANCE	0x8084

/* URE_PLA_RCR */
#define	URE_RCR_AAP		0x00000001
#define	URE_RCR_APM		0x00000002
#define	URE_RCR_AM		0x00000004
#define	URE_RCR_AB		0x00000008
#define	URE_RCR_ACPT_ALL	\
	(URE_RCR_AAP | URE_RCR_APM | URE_RCR_AM | URE_RCR_AB)
#define	URE_SLOT_EN		0x00000800

/* URE_PLA_RCR1 */
#define	URE_INNER_VLAN		0x0040
#define	URE_OUTER_VLAN		0x0080

/* URE_PLA_RXFIFO_CTRL0 */
#define	URE_RXFIFO_THR1_NORMAL	0x00080002
#define	URE_RXFIFO_THR1_OOB	0x01800003

/* URE_PLA_RXFIFO_FULL */
#define URE_RXFIFO_FULL_MASK	0x0fff

/* URE_PLA_RXFIFO_CTRL1 */
#define	URE_RXFIFO_THR2_FULL	0x00000060
#define	URE_RXFIFO_THR2_HIGH	0x00000038
#define	URE_RXFIFO_THR2_OOB	0x0000004a
#define	URE_RXFIFO_THR2_NORMAL	0x00a0

/* URE_PLA_RXFIFO_CTRL2 */
#define	URE_RXFIFO_THR3_FULL	0x00000078
#define	URE_RXFIFO_THR3_HIGH	0x00000048
#define	URE_RXFIFO_THR3_OOB	0x0000005a
#define	URE_RXFIFO_THR3_NORMAL	0x0110

/* URE_PLA_TXFIFO_CTRL */
#define	URE_TXFIFO_THR_NORMAL	0x00400008
#define	URE_TXFIFO_THR_NORMAL2	0x01000008

/* URE_PLA_DMY_REG0 */
#define	URE_ECM_ALDPS		0x0002

/* URE_PLA_FMC */
#define	URE_FMC_FCR_MCU_EN	0x0001

/* URE_PLA_EEEP_CR */
#define	URE_EEEP_CR_EEEP_TX	0x0002

/* URE_PLA_WDT6_CTRL */
#define	URE_WDT6_SET_MODE	0x0010

/* URE_PLA_TCR0 */
#define	URE_TCR0_AUTO_FIFO	0x0080
#define	URE_TCR0_TX_EMPTY	0x0800

/* URE_PLA_TCR1 */
#define	URE_VERSION_MASK	0x7cf0

/* URE_PLA_MTPS */
#define	MTPS_DEFAULT		96
#define	MTPS_JUMBO		192
#define	MTPS_MAX		255

/* URE_PLA_RSTTALLY */
#define	URE_TALLY_RESET		0x0001

/* URE_PLA_CR */
#define	URE_CR_RST		0x10
#define	URE_CR_RE		0x08
#define	URE_CR_TE		0x04

/* URE_PLA_CRWECR */
#define	URE_CRWECR_NORAML	0x00
#define	URE_CRWECR_CONFIG	0xc0

/* URE_PLA_OOB_CTRL */
#define	URE_DIS_MCU_CLROOB	0x01
#define	URE_LINK_LIST_READY	0x02
#define	URE_RXFIFO_EMPTY	0x10
#define	URE_TXFIFO_EMPTY	0x20
#define	URE_NOW_IS_OOB		0x80
#define	URE_FIFO_EMPTY		(URE_TXFIFO_EMPTY | URE_RXFIFO_EMPTY)

/* URE_PLA_MISC_1 */
#define	URE_RXDY_GATED_EN	0x0008

/* URE_PLA_SFF_STS_7 */
#define	URE_MCU_BORW_EN		0x4000
#define	URE_RE_INIT_LL		0x8000

/* URE_PLA_CPCR */
#define	URE_FLOW_CTRL_EN	0x0001
#define	URE_CPCR_RX_VLAN	0x0040

/* URE_PLA_TEREDO_CFG */
#define	URE_TEREDO_SEL			0x8000
#define	URE_TEREDO_WAKE_MASK		0x7f00
#define	URE_TEREDO_RS_EVENT_MASK	0x00fe
#define	URE_OOB_TEREDO_EN		0x0001

/* URE_PLA_BDC_CR */
#define	URE_ALDPS_PROXY_MODE	0x0001

/* URE_PLA_CONFIG34 */
#define	URE_LINK_OFF_WAKE_EN	0x0008
#define	URE_LINK_ON_WAKE_EN	0x0010

/* URE_PLA_CONFIG5 */
#define	URE_LAN_WAKE_EN		0x0002

/* URE_PLA_LED_FEATURE */
#define	URE_LED_MODE_MASK	0x0700

/* URE_PLA_PHY_PWR */
#define	URE_TX_10M_IDLE_EN	0x0080
#define	URE_PFM_PWM_SWITCH	0x0040

/* URE_PLA_MAC_PWR_CTRL */
#define	URE_D3_CLK_GATED_EN	0x00004000
#define	URE_MCU_CLK_RATIO	0x07010f07
#define	URE_MCU_CLK_RATIO_MASK	0x0f0f0f0f
#define	URE_ALDPS_SPDWN_RATIO	0x0f87

/* URE_PLA_MAC_PWR_CTRL2 */
#define	URE_MAC_CLK_SPDWN_EN		0x8000
#define	URE_EEE_SPDWN_RATIO		0x8007
#define	URE_EEE_SPDWN_RATIO_MASK	0x00ff

/* URE_PLA_MAC_PWR_CTRL3 */
#define	URE_L1_SPDWN_EN		0x0001
#define	URE_U1U2_SPDWN_EN	0x0002
#define	URE_SUSPEND_SPDWN_EN	0x0004
#define	URE_PKT_AVAIL_SPDWN_EN	0x0100
#define	URE_PLA_MCU_SPDWN_EN	0x4000

/* URE_PLA_MAC_PWR_CTRL4 */
#define	URE_EEE_SPDWN_EN	0x0001
#define	URE_TP1000_SPDWN_EN	0x0008
#define	URE_TP500_SPDWN_EN	0x0010
#define	URE_TP100_SPDWN_EN	0x0020
#define	URE_IDLE_SPDWN_EN	0x0040
#define	URE_TX10MIDLE_EN	0x0100
#define	URE_RXDV_SPDWN_EN	0x0800
#define	URE_PWRSAVE_SPDWN_EN	0x1000

/* URE_PLA_GPHY_INTR_IMR */
#define	URE_GPHY_STS_MSK	0x0001
#define	URE_SPEED_DOWN_MSK	0x0002
#define	URE_SPDWN_RXDV_MSK	0x0004
#define	URE_SPDWN_LINKCHG_MSK	0x0008

/* URE_PLA_PHYAR */
#define	URE_PHYAR_PHYDATA	0x0000ffff
#define	URE_PHYAR_BUSY		0x80000000

/* URE_PLA_EEE_CR */
#define	URE_EEE_RX_EN		0x0001
#define	URE_EEE_TX_EN		0x0002

/* URE_PLA_BOOT_CTRL */
#define	URE_AUTOLOAD_DONE	0x0002

/* URE_PLA_LWAKE_CTRL_REG */
#define URE_LANWAKE_PIN		0x80

/* URE_PLA_SUSPEND_FLAG */
#define	URE_LINK_CHG_EVENT	0x01

/* URE_PLA_INDICATE_FALG */
#define	URE_UPCOMING_RUNTIME_D3	0x01

/* URE_PLA_EXTRA_STATUS */
#define	URE_POLL_LINK_CHG	0x0001
#define	URE_LINK_CHANGE_FLAG	0x0100
#define	URE_CUR_LINK_OK		0x8000

/* URE_PLA_GPHY_CTRL */
#define	URE_GPHY_FLASH		0x0002

/* URE_PLA_POL_GPIO_CTRL */
#define	URE_DACK_DET_EN		0x8000

/* URE_PLA_PHYSTATUS */
#define URE_PHYSTATUS_FDX	0x0001
#define URE_PHYSTATUS_LINK	0x0002
#define URE_PHYSTATUS_10MBPS	0x0004
#define URE_PHYSTATUS_100MBPS	0x0008
#define URE_PHYSTATUS_1000MBPS	0x0010
#define URE_PHYSTATUS_2500MBPS	0x0400
#define URE_PHYSTATUS_5000MBPS	0x1000

/* URE_PLA_CONFIG6 */
#define	URE_LANWAKE_CLR_EN	0x01

/* URE_USB_USB2PHY */
#define	URE_USB2PHY_SUSPEND	0x0001
#define	URE_USB2PHY_L1		0x0002

/* URE_USB_SSPHYLINK1 */
#define	URE_DELAY_PHY_PWR_CHG	0x0002

/* URE_USB_SSPHYLINK2 */
#define	URE_PWD_DN_SCALE_MASK	0x3ffe
#define	URE_PWD_DN_SCALE(x)	((x) << 1)

/* URE_USB_CSR_DUMMY1 */
#define	URE_DYNAMIC_BURST	0x0001

/* URE_USB_CSR_DUMMY2 */
#define	URE_EP4_FULL_FC		0x0001

/* URE_USB_DEV_STAT */
#define	URE_STAT_SPEED_HIGH	0x0000
#define	URE_STAT_SPEED_FULL	0x0001
#define	URE_STAT_SPEED_MASK	0x0006

/* URE_USB_LPM_CONFIG */
#define LPM_U1U2_EN		0x0001

/* URE_USB_MISC_2 */
#define	URE_UPS_FORCE_PWR_DOWN	0x01
#define	URE_UPS_NO_UPS		0x80

/* URE_USB_ECM_OPTION */
#define	URE_BYPASS_MAC_RESET	0x0020

/* URE_USB_GPHY_CTRL */
#define	URE_GPHY_PATCH_DONE	0x0004
#define	URE_BYPASS_FLASH	0x0020

/* URE_USB_SPEED_OPTION */
#define	URE_RG_PWRDN_EN		0x0100
#define	URE_ALL_SPEED_OFF	0x0200

/* URE_USB_FW_CTRL */
#define	URE_FLOW_CTRL_PATCH_OPT	0x0002
#define	URE_AUTO_SPEEDUP	0x0008
#define	URE_FLOW_CTRL_PATCH_2	0x0100

/* URE_URE_USB_FC_TIMER */
#define	URE_CTRL_TIMER_EN	0x8000

/* URE_USB_USB_ECM_OP */
#define	URE_EN_ALL_SPEED	0x0001

/* URE_USB_TX_AGG */
#define	URE_TX_AGG_MAX_THRESHOLD	0x03

/* URE_USB_RX_BUF_TH */
#define	URE_RX_THR_SUPER	0x0c350180
#define	URE_RX_THR_HIGH		0x7a120180
#define	URE_RX_THR_SLOW		0xffff0180
#define	URE_RX_THR_B		0x00010001

/* URE_USB_TX_DMA */
#define	URE_TEST_MODE_DISABLE	0x00000001
#define	URE_TX_SIZE_ADJUST1	0x00000100

/* URE_USB_UPT_RXDMA_OWN */
#define	URE_OWN_UPDATE		0x01
#define	URE_OWN_CLEAR		0x02

/* URE_USB_BMU_RESET */
#define	BMU_RESET_EP_IN		0x01
#define	BMU_RESET_EP_OUT	0x02

/* URE_USB_BMU_CONFIG */
#define	URE_ACT_ODMA		0x02

/* URE_USB_FW_TASK */
#define	URE_FC_PATCH_TASK	0x0002

/* URE_USB_RX_AGGR_NUM */
#define	URE_RX_AGGR_NUM_MASK	0x1ff

/* URE_USB_CMD */
#define URE_CMD_BMU		0x0000
#define URE_CMD_BUSY		0x0001
#define URE_CMD_WRITE		0x0002
#define URE_CMD_IP		0x0004

/* URE_USB_TGPHY_CMD */
#define URE_TGPHY_CMD_BUSY	0x0001
#define URE_TGPHY_CMD_WRITE	0x0002

/* URE_USB_UPS_CTRL */
#define	URE_POWER_CUT		0x0100

/* URE_USB_PM_CTRL_STATUS */
#define	URE_RESUME_INDICATE	0x0001

/* URE_USB_USB_CTRL */
#define	URE_CDC_ECM_EN		0x0008
#define	URE_RX_AGG_DISABLE	0x0010
#define	URE_RX_ZERO_EN		0x0080

/* URE_USB_U2P3_CTRL */
#define	URE_U2P3_ENABLE		0x0001
#define	URE_RX_DETECT8		0x0008

/* URE_USB_U2P3_CTRL2 */
#define URE_U2P3_CTRL2_ENABLE	0x20000000

/* URE_USB_POWER_CUT */
#define	URE_PWR_EN		0x0001
#define	URE_PHASE2_EN		0x0008
#define	URE_UPS_EN		0x0010
#define	URE_USP_PREWAKE		0x0020

/* URE_USB_MISC_0 */
#define	URE_PCUT_STATUS		0x0001

/* URE_USB_RX_EARLY_AGG */
#define	URE_COALESCE_SUPER	85000U
#define	URE_COALESCE_HIGH	250000U
#define	URE_COALESCE_SLOW	524280U

/* URE_USB_WDT11_CTRL */
#define	URE_TIMER11_EN		0x0001

/* URE_USB_LPM_CTRL */
#define	URE_FIFO_EMPTY_1FB	0x30
#define	URE_LPM_TIMER_MASK	0x0c
#define	URE_LPM_TIMER_500MS	0x04
#define	URE_LPM_TIMER_500US	0x0c
#define	URE_ROK_EXIT_LPM	0x02

/* URE_USB_AFE_CTRL2 */
#define	URE_SEN_VAL_MASK	0xf800
#define	URE_SEN_VAL_NORMAL	0xa000
#define	URE_SEL_RXIDLE		0x0100

/* URE_USB_UPS_FLAGS */
#define	URE_UPS_FLAGS_EN_ALDPS	0x00000008
#define URE_UPS_FLAGS_MASK	0xffffffff

/* URE_OCP_ALDPS_CONFIG */
#define	URE_ENPWRSAVE		0x8000
#define	URE_ENPDNPS		0x0200
#define	URE_LINKENA		0x0100
#define	URE_DIS_SDSAVE		0x0010

/* URE_OCP_PHY_STATUS */
#define	URE_PHY_STAT_MASK	0x0007
#define	URE_PHY_STAT_EXT_INIT	2
#define	URE_PHY_STAT_LAN_ON	3
#define	URE_PHY_STAT_PWRDN	5

/* URE_OCP_POWER_CFG */
#define	URE_EEE_CLKDIV_EN	0x8000
#define	URE_EN_ALDPS		0x0004
#define	URE_EN_10M_PLLOFF	0x0001

/* URE_OCP_EEE_CFG */
#define	URE_CTAP_SHORT_EN	0x0040
#define	URE_EEE10_EN		0x0010

/* URE_OCP_DOWN_SPEED */
#define	URE_EN_10M_BGOFF	0x0080

/* URE_OCP_PHY_STATE */
#define	URE_TXDIS_STATE		0x01
#define	URE_ABD_STATE		0x02

/* URE_OCP_ADC_CFG */
#define	URE_EN_EMI_L		0x0040
#define	URE_ADC_EN		0x0080
#define	URE_CKADSEL_L		0x0100

#define URE_ADV_2500TFDX	0x0080
#define URE_ADV_5000TFDX	0x0100

#define	URE_MCU_TYPE_PLA	0x0100
#define	URE_MCU_TYPE_USB	0x0000

#define	GET_MII(sc)		uether_getmii(&(sc)->sc_ue)

struct ure_intrpkt {
	uint8_t	ure_tsr;
	uint8_t	ure_rsr;
	uint8_t	ure_gep_msr;
	uint8_t	ure_waksr;
	uint8_t	ure_txok_cnt;
	uint8_t	ure_rxlost_cnt;
	uint8_t	ure_crcerr_cnt;
	uint8_t	ure_col_cnt;
} __packed;

struct ure_rxpkt {
	uint32_t ure_pktlen;
#define	URE_RXPKT_LEN_MASK	0x7fff
	uint32_t ure_vlan;
#define	URE_RXPKT_UDP		(1 << 23)
#define	URE_RXPKT_TCP		(1 << 22)
#define	URE_RXPKT_IPV6		(1 << 20)
#define	URE_RXPKT_IPV4		(1 << 19)
#define	URE_RXPKT_VLAN_TAG	(1 << 16)
#define	URE_RXPKT_VLAN_DATA	0xffff
	uint32_t ure_csum;
#define	URE_RXPKT_IPSUMBAD	(1 << 23)
#define	URE_RXPKT_UDPSUMBAD	(1 << 22)
#define	URE_RXPKT_TCPSUMBAD	(1 << 21)
	uint32_t ure_rsvd2;
	uint32_t ure_rsvd3;
	uint32_t ure_rsvd4;
} __packed;

struct ure_txpkt {
	uint32_t ure_pktlen;
#define	URE_TXPKT_TX_FS		(1U << 31)
#define	URE_TXPKT_TX_LS		(1 << 30)
#define	URE_TXPKT_LEN_MASK	0xffff
	uint32_t ure_vlan;
#define	URE_TXPKT_UDP		(1U << 31)
#define	URE_TXPKT_TCP		(1 << 30)
#define	URE_TXPKT_IPV4		(1 << 29)
#define	URE_TXPKT_IPV6		(1 << 28)
#define	URE_TXPKT_VLAN_TAG	(1 << 16)
} __packed;

struct ure_rxpkt_v2 {
	uint32_t ure_pktlen;
#define URE_RXPKT_V2_LEN_MASK	0xfffe0000
#define URE_RXPKT_V2_VLAN_TAG	(1 << 3)
	uint32_t ure_vlan;
	uint32_t ure_csum;
#define URE_RXPKT_V2_IPSUMBAD	(1 << 26)
#define URE_RXPKT_V2_UDPSUMBAD	(1 << 25)
#define URE_RXPKT_V2_TCPSUMBAD	(1 << 24)
#define URE_RXPKT_V2_IPV6	(1 << 15)
#define URE_RXPKT_V2_IPV4	(1 << 14)
#define URE_RXPKT_V2_UDP	(1 << 11)
#define URE_RXPKT_V2_TCP	(1 << 10)
	uint32_t ure_rsvd0;
} __packed;

struct ure_txpkt_v2 {
	uint32_t ure_cmdstat;
	uint32_t ure_vlan;
	uint32_t ure_pktlen;
	uint32_t ure_signature;
#define URE_TXPKT_SIGNATURE	0xa8000000
} __packed;

#define URE_ENDPT_RX		0
#define URE_ENDPT_TX		1
#define URE_ENDPT_MAX		2

#define	URE_TX_LIST_CNT		1
#define	URE_RX_LIST_CNT		1
#define	URE_TX_BUF_ALIGN	4
#define	URE_RX_BUF_ALIGN	8
#define	URE_8157_BUF_ALIGN	16

#define	URE_TX_BUFSZ		16384
#define	URE_8156_TX_BUFSZ	32768
#define	URE_8152_RX_BUFSZ	16384
#define	URE_8153_RX_BUFSZ	32768

#define URE_CMD_TYPE_BMU	0
#define URE_CMD_TYPE_IP		1

struct ure_chain {
	struct ure_softc	*uc_sc;
	struct usbd_xfer	*uc_xfer;
	char			*uc_buf;
	uint32_t		uc_cnt;
	uint32_t		uc_buflen;
	uint32_t		uc_bufmax;
	SLIST_ENTRY(ure_chain)  uc_list;
	uint8_t			uc_idx;
};

struct ure_cdata {
	struct ure_chain	ure_rx_chain[URE_RX_LIST_CNT];
	struct ure_chain	ure_tx_chain[URE_TX_LIST_CNT];
	SLIST_HEAD(ure_list_head, ure_chain)    ure_tx_free;
};

struct ure_softc {
	struct device		ure_dev;
	struct usbd_device	*ure_udev;

	/* usb */
	struct usbd_interface	*ure_iface;
	struct usb_task		ure_tick_task;
	int			ure_ed[URE_ENDPT_MAX];
	struct usbd_pipe	*ure_ep[URE_ENDPT_MAX];

	/* ethernet */
	struct arpcom		ure_ac;
	struct mii_data		ure_mii;
	struct ifmedia		ure_ifmedia;
	struct rwlock		ure_mii_lock;
	int			ure_refcnt;

	struct ure_cdata	ure_cdata;
	struct timeout		ure_stat_ch;

	struct timeval		ure_rx_notice;
	int			ure_rxbufsz;
	int			ure_txbufsz;

	int			ure_phyno;

	uint16_t		(*ure_phy_read)(struct ure_softc *, uint16_t);
	void			(*ure_phy_write)(struct ure_softc *, uint16_t,
				    uint16_t);

	u_int			ure_flags;
#define	URE_FLAG_LINK		0x0001
#define	URE_FLAG_8152		0x0010	/* RTL8152 */
#define	URE_FLAG_8153B		0x0020	/* RTL8153B */
#define	URE_FLAG_8156		0x0040	/* RTL8156 */
#define	URE_FLAG_8156B		0x0080	/* RTL8156B */
#define	URE_FLAG_8157		0x0100	/* RTL8157 */
#define	URE_FLAG_CHIP_MASK	0x01f0

	u_int			ure_chip;
#define	URE_CHIP_VER_4C00	0x01
#define	URE_CHIP_VER_4C10	0x02
#define	URE_CHIP_VER_5C00	0x04
#define	URE_CHIP_VER_5C10	0x08
#define	URE_CHIP_VER_5C20	0x10
#define	URE_CHIP_VER_5C30	0x20
#define	URE_CHIP_VER_6010	0x40
#define	URE_CHIP_VER_7420	0x80
};
