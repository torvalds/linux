/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Oleksandr Tymoshenko
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
 */

/* $FreeBSD$ */

#ifndef _AR71XX_REG_H_
#define _AR71XX_REG_H_

/* PCI region */
#define AR71XX_PCI_MEM_BASE		0x10000000
/* 
 * PCI mem windows is 0x08000000 bytes long but we exclude control 
 * region from the resource manager
 */
#define AR71XX_PCI_MEM_SIZE		0x07000000
#define AR71XX_PCI_IRQ_START		0
#define AR71XX_PCI_IRQ_END		2
#define AR71XX_PCI_NIRQS		3
/*
 * PCI devices slots are starting from this number
 */
#define	AR71XX_PCI_BASE_SLOT		17

/* PCI config registers */
#define	AR71XX_PCI_LCONF_CMD		0x17010000
#define			PCI_LCONF_CMD_READ	0x00000000
#define			PCI_LCONF_CMD_WRITE	0x00010000
#define	AR71XX_PCI_LCONF_WRITE_DATA	0x17010004
#define	AR71XX_PCI_LCONF_READ_DATA	0x17010008
#define	AR71XX_PCI_CONF_ADDR		0x1701000C
#define	AR71XX_PCI_CONF_CMD		0x17010010
#define			PCI_CONF_CMD_READ	0x0000000A
#define			PCI_CONF_CMD_WRITE	0x0000000B
#define	AR71XX_PCI_CONF_WRITE_DATA	0x17010014
#define	AR71XX_PCI_CONF_READ_DATA	0x17010018
#define	AR71XX_PCI_ERROR		0x1701001C
#define	AR71XX_PCI_ERROR_ADDR		0x17010020
#define	AR71XX_PCI_AHB_ERROR		0x17010024
#define	AR71XX_PCI_AHB_ERROR_ADDR	0x17010028

/* APB region */
/*
 * Size is not really true actual APB window size is 
 * 0x01000000 but it should handle OHCI memory as well
 * because this controller's interrupt is routed through 
 * APB. 
 */
#define AR71XX_APB_BASE         0x18000000
#define AR71XX_APB_SIZE         0x06000000

/* DDR registers */
#define AR71XX_DDR_CONFIG		0x18000000
#define AR71XX_DDR_CONFIG2		0x18000004
#define AR71XX_DDR_MODE_REGISTER	0x18000008
#define AR71XX_DDR_EXT_MODE_REGISTER	0x1800000C
#define AR71XX_DDR_CONTROL		0x18000010
#define AR71XX_DDR_REFRESH		0x18000014
#define AR71XX_DDR_RD_DATA_THIS_CYCLE	0x18000018
#define AR71XX_TAP_CONTROL0		0x1800001C
#define AR71XX_TAP_CONTROL1		0x18000020
#define AR71XX_TAP_CONTROL2		0x18000024
#define AR71XX_TAP_CONTROL3		0x18000028
#define AR71XX_PCI_WINDOW0		0x1800007C
#define AR71XX_PCI_WINDOW1		0x18000080
#define AR71XX_PCI_WINDOW2		0x18000084
#define AR71XX_PCI_WINDOW3		0x18000088
#define AR71XX_PCI_WINDOW4		0x1800008C
#define AR71XX_PCI_WINDOW5		0x18000090
#define AR71XX_PCI_WINDOW6		0x18000094
#define AR71XX_PCI_WINDOW7		0x18000098
#define AR71XX_WB_FLUSH_GE0		0x1800009C
#define AR71XX_WB_FLUSH_GE1		0x180000A0
#define AR71XX_WB_FLUSH_USB		0x180000A4
#define AR71XX_WB_FLUSH_PCI		0x180000A8

/*
 * Values for PCI_WINDOW_X registers 
 */
#define PCI_WINDOW0_ADDR		0x10000000
#define PCI_WINDOW1_ADDR		0x11000000
#define PCI_WINDOW2_ADDR		0x12000000
#define PCI_WINDOW3_ADDR		0x13000000
#define PCI_WINDOW4_ADDR		0x14000000
#define PCI_WINDOW5_ADDR		0x15000000
#define PCI_WINDOW6_ADDR		0x16000000
#define PCI_WINDOW7_ADDR		0x17000000
/* This value enables acces to PCI config registers */
#define PCI_WINDOW7_CONF_ADDR		0x07000000

#define	AR71XX_UART_ADDR		0x18020000
#define		AR71XX_UART_THR		0x0
#define		AR71XX_UART_LSR		0x14
#define		AR71XX_UART_LSR_THRE	(1 << 5)
#define		AR71XX_UART_LSR_TEMT	(1 << 6)

#define	AR71XX_USB_CTRL_FLADJ		0x18030000
#define		USB_CTRL_FLADJ_HOST_SHIFT	12
#define		USB_CTRL_FLADJ_A5_SHIFT		10
#define		USB_CTRL_FLADJ_A4_SHIFT		8
#define		USB_CTRL_FLADJ_A3_SHIFT		6
#define		USB_CTRL_FLADJ_A2_SHIFT		4
#define		USB_CTRL_FLADJ_A1_SHIFT		2
#define		USB_CTRL_FLADJ_A0_SHIFT		0
#define	AR71XX_USB_CTRL_CONFIG		0x18030004
#define		USB_CTRL_CONFIG_OHCI_DES_SWAP	(1 << 19)
#define		USB_CTRL_CONFIG_OHCI_BUF_SWAP	(1 << 18)
#define		USB_CTRL_CONFIG_EHCI_DES_SWAP	(1 << 17)
#define		USB_CTRL_CONFIG_EHCI_BUF_SWAP	(1 << 16)
#define		USB_CTRL_CONFIG_DISABLE_XTL	(1 << 13)
#define		USB_CTRL_CONFIG_OVERRIDE_XTL	(1 << 12)
#define		USB_CTRL_CONFIG_CLK_SEL_SHIFT	4
#define		USB_CTRL_CONFIG_CLK_SEL_MASK	3
#define		USB_CTRL_CONFIG_CLK_SEL_12	0
#define		USB_CTRL_CONFIG_CLK_SEL_24	1
#define		USB_CTRL_CONFIG_CLK_SEL_48	2
#define		USB_CTRL_CONFIG_OVER_CURRENT_AS_GPIO	(1 << 8)
#define		USB_CTRL_CONFIG_SS_SIMULATION_MODE	(1 << 2)
#define		USB_CTRL_CONFIG_RESUME_UTMI_PLS_DIS	(1 << 1)
#define		USB_CTRL_CONFIG_UTMI_BACKWARD_ENB	(1 << 0)

#define	AR71XX_GPIO_BASE		0x18040000
#define		AR71XX_GPIO_OE			0x00
#define		AR71XX_GPIO_IN			0x04
#define		AR71XX_GPIO_OUT			0x08
#define		AR71XX_GPIO_SET			0x0c
#define		AR71XX_GPIO_CLEAR		0x10
#define		AR71XX_GPIO_INT			0x14
#define		AR71XX_GPIO_INT_TYPE		0x18
#define		AR71XX_GPIO_INT_POLARITY	0x1c
#define		AR71XX_GPIO_INT_PENDING		0x20
#define		AR71XX_GPIO_INT_MASK		0x24
#define		AR71XX_GPIO_FUNCTION		0x28
#define			GPIO_FUNC_STEREO_EN     (1 << 17)
#define			GPIO_FUNC_SLIC_EN       (1 << 16)
#define			GPIO_FUNC_SPI_CS2_EN    (1 << 13)
				/* CS2 is shared with GPIO_1 */
#define			GPIO_FUNC_SPI_CS1_EN    (1 << 12)
				/* CS1 is shared with GPIO_0 */
#define			GPIO_FUNC_UART_EN       (1 << 8)
#define			GPIO_FUNC_USB_OC_EN     (1 << 4)
#define			GPIO_FUNC_USB_CLK_EN    (0)

#define	AR71XX_BASE_FREQ		40000000
#define	AR71XX_PLL_CPU_BASE		0x18050000
#define	AR71XX_PLL_CPU_CONFIG		0x18050000
#define		PLL_SW_UPDATE			(1U << 31)
#define		PLL_LOCKED			(1 << 30)
#define		PLL_AHB_DIV_SHIFT		20
#define		PLL_AHB_DIV_MASK		7
#define		PLL_DDR_DIV_SEL_SHIFT		18
#define		PLL_DDR_DIV_SEL_MASK		3
#define		PLL_CPU_DIV_SEL_SHIFT		16
#define		PLL_CPU_DIV_SEL_MASK		3
#define		PLL_LOOP_BW_SHIFT		12
#define		PLL_LOOP_BW_MASK		0xf
#define		PLL_DIV_IN_SHIFT		10
#define		PLL_DIV_IN_MASK			3
#define		PLL_DIV_OUT_SHIFT		8
#define		PLL_DIV_OUT_MASK		3
#define		PLL_FB_SHIFT			3
#define		PLL_FB_MASK			0x1f
#define		PLL_BYPASS			(1 << 1)
#define		PLL_POWER_DOWN			(1 << 0)
#define	AR71XX_PLL_SEC_CONFIG		0x18050004
#define		AR71XX_PLL_ETH0_SHIFT		17
#define		AR71XX_PLL_ETH1_SHIFT		19
#define	AR71XX_PLL_CPU_CLK_CTRL		0x18050008
#define	AR71XX_PLL_ETH_INT0_CLK		0x18050010
#define	AR71XX_PLL_ETH_INT1_CLK		0x18050014
#define		XPLL_ETH_INT_CLK_10		0x00991099
#define		XPLL_ETH_INT_CLK_100		0x00441011
#define		XPLL_ETH_INT_CLK_1000		0x13110000
#define		XPLL_ETH_INT_CLK_1000_GMII	0x14110000
#define		PLL_ETH_INT_CLK_10		0x00991099
#define		PLL_ETH_INT_CLK_100		0x00001099
#define		PLL_ETH_INT_CLK_1000		0x00110000
#define	AR71XX_PLL_ETH_EXT_CLK		0x18050018
#define	AR71XX_PLL_PCI_CLK		0x1805001C

/* Reset block */
#define	AR71XX_RST_BLOCK_BASE	0x18060000

#define AR71XX_RST_WDOG_CONTROL	0x18060008
#define		RST_WDOG_LAST			(1U << 31)
#define		RST_WDOG_ACTION_MASK		3
#define		RST_WDOG_ACTION_RESET		3
#define		RST_WDOG_ACTION_NMI		2
#define		RST_WDOG_ACTION_GP_INTR		1
#define		RST_WDOG_ACTION_NOACTION	0

#define AR71XX_RST_WDOG_TIMER	0x1806000C
/* 
 * APB interrupt status and mask register and interrupt bit numbers for 
 */
#define AR71XX_MISC_INTR_STATUS	0x18060010
#define AR71XX_MISC_INTR_MASK	0x18060014
#define		MISC_INTR_TIMER		0
#define		MISC_INTR_ERROR		1
#define		MISC_INTR_GPIO		2
#define		MISC_INTR_UART		3
#define		MISC_INTR_WATCHDOG	4
#define		MISC_INTR_PERF		5
#define		MISC_INTR_OHCI		6
#define		MISC_INTR_DMA		7

#define AR71XX_PCI_INTR_STATUS	0x18060018
#define AR71XX_PCI_INTR_MASK	0x1806001C
#define		PCI_INTR_CORE		(1 << 4)

#define AR71XX_RST_RESET	0x18060024
#define		RST_RESET_FULL_CHIP	(1 << 24) /* Same as pulling
							     the reset pin */
#define		RST_RESET_CPU_COLD	(1 << 20) /* Cold reset */
#define		RST_RESET_GE1_MAC	(1 << 13)
#define		RST_RESET_GE1_PHY	(1 << 12)
#define		RST_RESET_GE0_MAC	(1 <<  9)
#define		RST_RESET_GE0_PHY	(1 <<  8)
#define		RST_RESET_USB_OHCI_DLL	(1 <<  6)
#define		RST_RESET_USB_HOST	(1 <<  5)
#define		RST_RESET_USB_PHY	(1 <<  4)
#define		RST_RESET_PCI_BUS	(1 <<  1)
#define		RST_RESET_PCI_CORE	(1 <<  0)

/* Chipset revision details */
#define	AR71XX_RST_RESET_REG_REV_ID	0x18060090
#define		REV_ID_MAJOR_MASK	0xfff0
#define		REV_ID_MAJOR_AR71XX	0x00a0
#define		REV_ID_MAJOR_AR913X	0x00b0
#define		REV_ID_MAJOR_AR7240	0x00c0
#define		REV_ID_MAJOR_AR7241	0x0100
#define		REV_ID_MAJOR_AR7242	0x1100

/* AR71XX chipset revision details */
#define		AR71XX_REV_ID_MINOR_MASK	0x3
#define		AR71XX_REV_ID_MINOR_AR7130	0x0
#define		AR71XX_REV_ID_MINOR_AR7141	0x1
#define		AR71XX_REV_ID_MINOR_AR7161	0x2
#define		AR71XX_REV_ID_REVISION_MASK	0x3
#define		AR71XX_REV_ID_REVISION_SHIFT	2

/* AR724X chipset revision details */
#define		AR724X_REV_ID_REVISION_MASK	0x3

/* AR91XX chipset revision details */
#define		AR91XX_REV_ID_MINOR_MASK	0x3
#define		AR91XX_REV_ID_MINOR_AR9130	0x0
#define		AR91XX_REV_ID_MINOR_AR9132	0x1
#define		AR91XX_REV_ID_REVISION_MASK	0x3
#define		AR91XX_REV_ID_REVISION_SHIFT	2

typedef enum {
	AR71XX_MII_MODE_NONE = 0,
	AR71XX_MII_MODE_GMII,
	AR71XX_MII_MODE_MII,
	AR71XX_MII_MODE_RGMII,
	AR71XX_MII_MODE_RMII,
	AR71XX_MII_MODE_SGMII	/* not hardware defined, though! */
} ar71xx_mii_mode;

/*
 * AR71xx MII control region
 */
#define	AR71XX_MII0_CTRL	0x18070000
#define			MII_CTRL_SPEED_SHIFT	4
#define			MII_CTRL_SPEED_MASK	3
#define				MII_CTRL_SPEED_10	0
#define				MII_CTRL_SPEED_100	1
#define				MII_CTRL_SPEED_1000	2
#define			MII_CTRL_IF_MASK	3
#define			MII_CTRL_IF_SHIFT	0
#define				MII0_CTRL_IF_GMII	0
#define				MII0_CTRL_IF_MII	1
#define				MII0_CTRL_IF_RGMII	2
#define				MII0_CTRL_IF_RMII	3

#define	AR71XX_MII1_CTRL	0x18070004

#define				MII1_CTRL_IF_RGMII	0
#define				MII1_CTRL_IF_RMII	1

/*
 * GigE adapters region
 */
#define AR71XX_MAC0_BASE	0x19000000
#define AR71XX_MAC1_BASE	0x1A000000

#define		AR71XX_MAC_CFG1			0x00
#define			MAC_CFG1_SOFT_RESET		(1U << 31)
#define			MAC_CFG1_SIMUL_RESET		(1 << 30)
#define			MAC_CFG1_MAC_RX_BLOCK_RESET	(1 << 19)
#define			MAC_CFG1_MAC_TX_BLOCK_RESET	(1 << 18)
#define			MAC_CFG1_RX_FUNC_RESET		(1 << 17)
#define			MAC_CFG1_TX_FUNC_RESET		(1 << 16)
#define			MAC_CFG1_LOOPBACK		(1 <<  8)
#define			MAC_CFG1_RXFLOW_CTRL		(1 <<  5)
#define			MAC_CFG1_TXFLOW_CTRL		(1 <<  4)
#define			MAC_CFG1_SYNC_RX		(1 <<  3)
#define			MAC_CFG1_RX_ENABLE		(1 <<  2)
#define			MAC_CFG1_SYNC_TX		(1 <<  1)
#define			MAC_CFG1_TX_ENABLE		(1 <<  0)
#define		AR71XX_MAC_CFG2			0x04
#define			MAC_CFG2_PREAMBLE_LEN_MASK	0xf
#define			MAC_CFG2_PREAMBLE_LEN_SHIFT	12
#define			MAC_CFG2_IFACE_MODE_1000	(2 << 8)
#define			MAC_CFG2_IFACE_MODE_10_100	(1 << 8)
#define			MAC_CFG2_IFACE_MODE_SHIFT	8
#define			MAC_CFG2_IFACE_MODE_MASK	3
#define			MAC_CFG2_HUGE_FRAME		(1 << 5)
#define			MAC_CFG2_LENGTH_FIELD		(1 << 4)
#define			MAC_CFG2_ENABLE_PADCRC		(1 << 2)
#define			MAC_CFG2_ENABLE_CRC		(1 << 1)
#define			MAC_CFG2_FULL_DUPLEX		(1 << 0)
#define		AR71XX_MAC_IFG			0x08
#define		AR71XX_MAC_HDUPLEX		0x0C
#define		AR71XX_MAC_MAX_FRAME_LEN	0x10
#define		AR71XX_MAC_MII_CFG		0x20
#define			MAC_MII_CFG_RESET		(1U << 31)
#define			MAC_MII_CFG_SCAN_AUTO_INC	(1 <<  5)
#define			MAC_MII_CFG_PREAMBLE_SUP	(1 <<  4)
#define			MAC_MII_CFG_CLOCK_SELECT_MASK	0x7
#define			MAC_MII_CFG_CLOCK_SELECT_MASK_AR933X	0xf
#define			MAC_MII_CFG_CLOCK_DIV_4		0
#define			MAC_MII_CFG_CLOCK_DIV_6		2
#define			MAC_MII_CFG_CLOCK_DIV_8		3
#define			MAC_MII_CFG_CLOCK_DIV_10	4
#define			MAC_MII_CFG_CLOCK_DIV_14	5
#define			MAC_MII_CFG_CLOCK_DIV_20	6
#define			MAC_MII_CFG_CLOCK_DIV_28	7

/* .. and the AR933x/AR934x extensions */
#define			MAC_MII_CFG_CLOCK_DIV_34	8
#define			MAC_MII_CFG_CLOCK_DIV_42	9
#define			MAC_MII_CFG_CLOCK_DIV_50	10
#define			MAC_MII_CFG_CLOCK_DIV_58	11
#define			MAC_MII_CFG_CLOCK_DIV_66	12
#define			MAC_MII_CFG_CLOCK_DIV_74	13
#define			MAC_MII_CFG_CLOCK_DIV_82	14
#define			MAC_MII_CFG_CLOCK_DIV_98	15

#define		AR71XX_MAC_MII_CMD		0x24
#define			MAC_MII_CMD_SCAN_CYCLE		(1 << 1)
#define			MAC_MII_CMD_READ		1
#define			MAC_MII_CMD_WRITE		0
#define		AR71XX_MAC_MII_ADDR		0x28
#define			MAC_MII_PHY_ADDR_SHIFT		8
#define			MAC_MII_PHY_ADDR_MASK		0xff
#define			MAC_MII_REG_MASK		0x1f
#define		AR71XX_MAC_MII_CONTROL		0x2C
#define			MAC_MII_CONTROL_MASK		0xffff
#define		AR71XX_MAC_MII_STATUS		0x30
#define			MAC_MII_STATUS_MASK		0xffff
#define		AR71XX_MAC_MII_INDICATOR	0x34
#define			MAC_MII_INDICATOR_NOT_VALID	(1 << 2)
#define			MAC_MII_INDICATOR_SCANNING	(1 << 1)
#define			MAC_MII_INDICATOR_BUSY		(1 << 0)
#define		AR71XX_MAC_IFCONTROL		0x38
#define			MAC_IFCONTROL_SPEED	(1 << 16)
#define		AR71XX_MAC_STA_ADDR1		0x40
#define		AR71XX_MAC_STA_ADDR2		0x44
#define		AR71XX_MAC_FIFO_CFG0		0x48
#define			FIFO_CFG0_TX_FABRIC		(1 << 4)
#define			FIFO_CFG0_TX_SYSTEM		(1 << 3)
#define			FIFO_CFG0_RX_FABRIC		(1 << 2)
#define			FIFO_CFG0_RX_SYSTEM		(1 << 1)
#define			FIFO_CFG0_WATERMARK		(1 << 0)
#define			FIFO_CFG0_ALL			((1 << 5) - 1)
#define			FIFO_CFG0_ENABLE_SHIFT		8
#define		AR71XX_MAC_FIFO_CFG1		0x4C
#define		AR71XX_MAC_FIFO_CFG2		0x50
#define		AR71XX_MAC_FIFO_TX_THRESHOLD	0x54
#define		AR71XX_MAC_FIFO_RX_FILTMATCH	0x58
/* 
 * These flags applicable both to AR71XX_MAC_FIFO_RX_FILTMASK and
 * to AR71XX_MAC_FIFO_RX_FILTMATCH
 */
#define			FIFO_RX_MATCH_UNICAST		(1 << 17)
#define			FIFO_RX_MATCH_TRUNC_FRAME	(1 << 16)
#define			FIFO_RX_MATCH_VLAN_TAG		(1 << 15)
#define			FIFO_RX_MATCH_UNSUP_OPCODE	(1 << 14)
#define			FIFO_RX_MATCH_PAUSE_FRAME	(1 << 13)
#define			FIFO_RX_MATCH_CTRL_FRAME	(1 << 12)
#define			FIFO_RX_MATCH_LONG_EVENT	(1 << 11)
#define			FIFO_RX_MATCH_DRIBBLE_NIBBLE	(1 << 10)
#define			FIFO_RX_MATCH_BCAST		(1 <<  9)
#define			FIFO_RX_MATCH_MCAST		(1 <<  8)
#define			FIFO_RX_MATCH_OK		(1 <<  7)
#define			FIFO_RX_MATCH_OORANGE		(1 <<  6)
#define			FIFO_RX_MATCH_LEN_MSMTCH	(1 <<  5)
#define			FIFO_RX_MATCH_CRC_ERROR		(1 <<  4)
#define			FIFO_RX_MATCH_CODE_ERROR	(1 <<  3)
#define			FIFO_RX_MATCH_FALSE_CARRIER	(1 <<  2)
#define			FIFO_RX_MATCH_RX_DV_EVENT	(1 <<  1)
#define			FIFO_RX_MATCH_DROP_EVENT	(1 <<  0)
/*
 * Exclude unicast and truncated frames from matching
 */
#define			FIFO_RX_FILTMATCH_DEFAULT		\
				(FIFO_RX_MATCH_VLAN_TAG		| \
				FIFO_RX_MATCH_UNSUP_OPCODE	| \
				FIFO_RX_MATCH_PAUSE_FRAME	| \
				FIFO_RX_MATCH_CTRL_FRAME	| \
				FIFO_RX_MATCH_LONG_EVENT	| \
				FIFO_RX_MATCH_DRIBBLE_NIBBLE	| \
				FIFO_RX_MATCH_BCAST		| \
				FIFO_RX_MATCH_MCAST		| \
				FIFO_RX_MATCH_OK		| \
				FIFO_RX_MATCH_OORANGE		| \
				FIFO_RX_MATCH_LEN_MSMTCH	| \
				FIFO_RX_MATCH_CRC_ERROR		| \
				FIFO_RX_MATCH_CODE_ERROR	| \
				FIFO_RX_MATCH_FALSE_CARRIER	| \
				FIFO_RX_MATCH_RX_DV_EVENT	| \
				FIFO_RX_MATCH_DROP_EVENT)
#define		AR71XX_MAC_FIFO_RX_FILTMASK	0x5C
#define			FIFO_RX_MASK_BYTE_MODE		(1 << 19)
#define			FIFO_RX_MASK_NO_SHORT_FRAME	(1 << 18)
#define			FIFO_RX_MASK_BIT17		(1 << 17)
#define			FIFO_RX_MASK_BIT16		(1 << 16)
#define			FIFO_RX_MASK_TRUNC_FRAME	(1 << 15)
#define			FIFO_RX_MASK_LONG_EVENT		(1 << 14)
#define			FIFO_RX_MASK_VLAN_TAG		(1 << 13)
#define			FIFO_RX_MASK_UNSUP_OPCODE	(1 << 12)
#define			FIFO_RX_MASK_PAUSE_FRAME	(1 << 11)
#define			FIFO_RX_MASK_CTRL_FRAME		(1 << 10)
#define			FIFO_RX_MASK_DRIBBLE_NIBBLE	(1 <<  9)
#define			FIFO_RX_MASK_BCAST		(1 <<  8)
#define			FIFO_RX_MASK_MCAST		(1 <<  7)
#define			FIFO_RX_MASK_OK			(1 <<  6)
#define			FIFO_RX_MASK_OORANGE		(1 <<  5)
#define			FIFO_RX_MASK_LEN_MSMTCH		(1 <<  4)
#define			FIFO_RX_MASK_CODE_ERROR		(1 <<  3)
#define			FIFO_RX_MASK_FALSE_CARRIER	(1 <<  2)
#define			FIFO_RX_MASK_RX_DV_EVENT	(1 <<  1)
#define			FIFO_RX_MASK_DROP_EVENT		(1 <<  0)

/*
 *  Len. mismatch, unsup. opcode and short frmae bits excluded
 */
#define			FIFO_RX_FILTMASK_DEFAULT \
				(FIFO_RX_MASK_NO_SHORT_FRAME	| \
				FIFO_RX_MASK_BIT17		| \
				FIFO_RX_MASK_BIT16		| \
				FIFO_RX_MASK_TRUNC_FRAME	| \
				FIFO_RX_MASK_LONG_EVENT		| \
				FIFO_RX_MASK_VLAN_TAG		| \
				FIFO_RX_MASK_PAUSE_FRAME	| \
				FIFO_RX_MASK_CTRL_FRAME		| \
				FIFO_RX_MASK_DRIBBLE_NIBBLE	| \
				FIFO_RX_MASK_BCAST		| \
				FIFO_RX_MASK_MCAST		| \
				FIFO_RX_MASK_OK			| \
				FIFO_RX_MASK_OORANGE		| \
				FIFO_RX_MASK_CODE_ERROR		| \
				FIFO_RX_MASK_FALSE_CARRIER	| \
				FIFO_RX_MASK_RX_DV_EVENT	| \
				FIFO_RX_MASK_DROP_EVENT)

#define		AR71XX_MAC_FIFO_RAM0		0x60
#define		AR71XX_MAC_FIFO_RAM1		0x64
#define		AR71XX_MAC_FIFO_RAM2		0x68
#define		AR71XX_MAC_FIFO_RAM3		0x6C
#define		AR71XX_MAC_FIFO_RAM4		0x70
#define		AR71XX_MAC_FIFO_RAM5		0x74
#define		AR71XX_MAC_FIFO_RAM6		0x78
#define		AR71XX_DMA_TX_CONTROL		0x180
#define			DMA_TX_CONTROL_EN		(1 << 0)
#define		AR71XX_DMA_TX_DESC		0x184
#define		AR71XX_DMA_TX_STATUS		0x188
#define			DMA_TX_STATUS_PCOUNT_MASK	0xff
#define			DMA_TX_STATUS_PCOUNT_SHIFT	16
#define			DMA_TX_STATUS_BUS_ERROR		(1 << 3) 
#define			DMA_TX_STATUS_UNDERRUN		(1 << 1) 
#define			DMA_TX_STATUS_PKT_SENT		(1 << 0) 
#define		AR71XX_DMA_RX_CONTROL		0x18C
#define			DMA_RX_CONTROL_EN		(1 << 0)
#define		AR71XX_DMA_RX_DESC		0x190
#define		AR71XX_DMA_RX_STATUS		0x194
#define			DMA_RX_STATUS_PCOUNT_MASK	0xff
#define			DMA_RX_STATUS_PCOUNT_SHIFT	16
#define			DMA_RX_STATUS_BUS_ERROR		(1 << 3)
#define			DMA_RX_STATUS_OVERFLOW		(1 << 2)
#define			DMA_RX_STATUS_PKT_RECVD		(1 << 0)
#define		AR71XX_DMA_INTR				0x198
#define		AR71XX_DMA_INTR_STATUS			0x19C
#define			DMA_INTR_ALL			((1 << 8) - 1)
#define			DMA_INTR_RX_BUS_ERROR		(1 << 7)
#define			DMA_INTR_RX_OVERFLOW		(1 << 6)
#define			DMA_INTR_RX_PKT_RCVD		(1 << 4)
#define			DMA_INTR_TX_BUS_ERROR		(1 << 3)
#define			DMA_INTR_TX_UNDERRUN		(1 << 1)
#define			DMA_INTR_TX_PKT_SENT		(1 << 0)

#define	AR71XX_SPI_BASE	0x1f000000
#define		AR71XX_SPI_FS		0x00
#define		AR71XX_SPI_CTRL		0x04
#define			SPI_CTRL_REMAP_DISABLE		(1 << 6)
#define			SPI_CTRL_CLOCK_DIVIDER_MASK	((1 << 6) - 1)
#define		AR71XX_SPI_IO_CTRL	0x08
#define			SPI_IO_CTRL_CS2			(1 << 18)
#define			SPI_IO_CTRL_CS1			(1 << 17)
#define			SPI_IO_CTRL_CS0			(1 << 16)
#define			SPI_IO_CTRL_CSMASK		(7 << 16)
#define			SPI_IO_CTRL_CLK			(1 << 8)
#define			SPI_IO_CTRL_DO			1
#define		AR71XX_SPI_RDS		0x0C

#define ATH_READ_REG(reg) \
	*((volatile uint32_t *)MIPS_PHYS_TO_KSEG1((reg)))
/*
 * Note: Don't put a flush read here; some users (eg the AR724x PCI fixup code)
 * requires write-only space to certain registers.  Doing the read afterwards
 * causes things to break.
 */
#define ATH_WRITE_REG(reg, val) \
      *((volatile uint32_t *)MIPS_PHYS_TO_KSEG1((reg))) = (val)

static inline void
ar71xx_ddr_flush(uint32_t reg)
{ 
	ATH_WRITE_REG(reg, 1);
	while ((ATH_READ_REG(reg) & 0x1))
		;
	ATH_WRITE_REG(reg, 1);
	while ((ATH_READ_REG(reg) & 0x1))
		;
} 

static inline void
ar71xx_write_pll(uint32_t cfg_reg, uint32_t pll_reg, uint32_t pll, uint32_t pll_reg_shift)
{
	uint32_t sec_cfg;

	/* set PLL registers */
	sec_cfg = ATH_READ_REG(cfg_reg);
	sec_cfg &= ~(3 << pll_reg_shift);
	sec_cfg |= (2 << pll_reg_shift);

	ATH_WRITE_REG(cfg_reg, sec_cfg);
	DELAY(100);

	ATH_WRITE_REG(pll_reg, pll);
	sec_cfg |= (3 << pll_reg_shift);
	ATH_WRITE_REG(cfg_reg, sec_cfg);
	DELAY(100);

	sec_cfg &= ~(3 << pll_reg_shift);
	ATH_WRITE_REG(cfg_reg, sec_cfg);
	DELAY(100);
}

#endif /* _AR71XX_REG_H_ */
