/*
 * Copyright (c) 2009-2011 Wind River Systems, Inc.
 * Copyright (c) 2011 ST Microelectronics (Alessandro Rubini)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * The STMicroelectronics ConneXt (STA2X11) chip has several unrelated
 * functions in one PCI endpoint functions. This driver simply
 * registers the platform devices in this iomemregion and exports a few
 * functions to access common registers
 */

#ifndef __STA2X11_MFD_H
#define __STA2X11_MFD_H
#include <linux/types.h>
#include <linux/pci.h>

enum sta2x11_mfd_plat_dev {
	sta2x11_sctl = 0,
	sta2x11_gpio,
	sta2x11_scr,
	sta2x11_time,
	sta2x11_apbreg,
	sta2x11_apb_soc_regs,
	sta2x11_vic,
	sta2x11_n_mfd_plat_devs,
};

#define STA2X11_MFD_SCTL_NAME	       "sta2x11-sctl"
#define STA2X11_MFD_GPIO_NAME	       "sta2x11-gpio"
#define STA2X11_MFD_SCR_NAME	       "sta2x11-scr"
#define STA2X11_MFD_TIME_NAME	       "sta2x11-time"
#define STA2X11_MFD_APBREG_NAME	       "sta2x11-apbreg"
#define STA2X11_MFD_APB_SOC_REGS_NAME  "sta2x11-apb-soc-regs"
#define STA2X11_MFD_VIC_NAME	       "sta2x11-vic"

extern u32
__sta2x11_mfd_mask(struct pci_dev *, u32, u32, u32, enum sta2x11_mfd_plat_dev);

/*
 * The MFD PCI block includes the GPIO peripherals and other register blocks.
 * For GPIO, we have 32*4 bits (I use "gsta" for "gpio sta2x11".)
 */
#define GSTA_GPIO_PER_BLOCK	32
#define GSTA_NR_BLOCKS		4
#define GSTA_NR_GPIO		(GSTA_GPIO_PER_BLOCK * GSTA_NR_BLOCKS)

/* Pinconfig is set by the board definition: altfunc, pull-up, pull-down */
struct sta2x11_gpio_pdata {
	unsigned pinconfig[GSTA_NR_GPIO];
};

/* Macros below lifted from sh_pfc.h, with minor differences */
#define PINMUX_TYPE_NONE		0
#define PINMUX_TYPE_FUNCTION		1
#define PINMUX_TYPE_OUTPUT_LOW		2
#define PINMUX_TYPE_OUTPUT_HIGH		3
#define PINMUX_TYPE_INPUT		4
#define PINMUX_TYPE_INPUT_PULLUP	5
#define PINMUX_TYPE_INPUT_PULLDOWN	6

/* Give names to GPIO pins, like PXA does, taken from the manual */
#define STA2X11_GPIO0			0
#define STA2X11_GPIO1			1
#define STA2X11_GPIO2			2
#define STA2X11_GPIO3			3
#define STA2X11_GPIO4			4
#define STA2X11_GPIO5			5
#define STA2X11_GPIO6			6
#define STA2X11_GPIO7			7
#define STA2X11_GPIO8_RGBOUT_RED7	8
#define STA2X11_GPIO9_RGBOUT_RED6	9
#define STA2X11_GPIO10_RGBOUT_RED5	10
#define STA2X11_GPIO11_RGBOUT_RED4	11
#define STA2X11_GPIO12_RGBOUT_RED3	12
#define STA2X11_GPIO13_RGBOUT_RED2	13
#define STA2X11_GPIO14_RGBOUT_RED1	14
#define STA2X11_GPIO15_RGBOUT_RED0	15
#define STA2X11_GPIO16_RGBOUT_GREEN7	16
#define STA2X11_GPIO17_RGBOUT_GREEN6	17
#define STA2X11_GPIO18_RGBOUT_GREEN5	18
#define STA2X11_GPIO19_RGBOUT_GREEN4	19
#define STA2X11_GPIO20_RGBOUT_GREEN3	20
#define STA2X11_GPIO21_RGBOUT_GREEN2	21
#define STA2X11_GPIO22_RGBOUT_GREEN1	22
#define STA2X11_GPIO23_RGBOUT_GREEN0	23
#define STA2X11_GPIO24_RGBOUT_BLUE7	24
#define STA2X11_GPIO25_RGBOUT_BLUE6	25
#define STA2X11_GPIO26_RGBOUT_BLUE5	26
#define STA2X11_GPIO27_RGBOUT_BLUE4	27
#define STA2X11_GPIO28_RGBOUT_BLUE3	28
#define STA2X11_GPIO29_RGBOUT_BLUE2	29
#define STA2X11_GPIO30_RGBOUT_BLUE1	30
#define STA2X11_GPIO31_RGBOUT_BLUE0	31
#define STA2X11_GPIO32_RGBOUT_VSYNCH	32
#define STA2X11_GPIO33_RGBOUT_HSYNCH	33
#define STA2X11_GPIO34_RGBOUT_DEN	34
#define STA2X11_GPIO35_ETH_CRS_DV	35
#define STA2X11_GPIO36_ETH_TXD1		36
#define STA2X11_GPIO37_ETH_TXD0		37
#define STA2X11_GPIO38_ETH_TX_EN	38
#define STA2X11_GPIO39_MDIO		39
#define STA2X11_GPIO40_ETH_REF_CLK	40
#define STA2X11_GPIO41_ETH_RXD1		41
#define STA2X11_GPIO42_ETH_RXD0		42
#define STA2X11_GPIO43_MDC		43
#define STA2X11_GPIO44_CAN_TX		44
#define STA2X11_GPIO45_CAN_RX		45
#define STA2X11_GPIO46_MLB_DAT		46
#define STA2X11_GPIO47_MLB_SIG		47
#define STA2X11_GPIO48_SPI0_CLK		48
#define STA2X11_GPIO49_SPI0_TXD		49
#define STA2X11_GPIO50_SPI0_RXD		50
#define STA2X11_GPIO51_SPI0_FRM		51
#define STA2X11_GPIO52_SPI1_CLK		52
#define STA2X11_GPIO53_SPI1_TXD		53
#define STA2X11_GPIO54_SPI1_RXD		54
#define STA2X11_GPIO55_SPI1_FRM		55
#define STA2X11_GPIO56_SPI2_CLK		56
#define STA2X11_GPIO57_SPI2_TXD		57
#define STA2X11_GPIO58_SPI2_RXD		58
#define STA2X11_GPIO59_SPI2_FRM		59
#define STA2X11_GPIO60_I2C0_SCL		60
#define STA2X11_GPIO61_I2C0_SDA		61
#define STA2X11_GPIO62_I2C1_SCL		62
#define STA2X11_GPIO63_I2C1_SDA		63
#define STA2X11_GPIO64_I2C2_SCL		64
#define STA2X11_GPIO65_I2C2_SDA		65
#define STA2X11_GPIO66_I2C3_SCL		66
#define STA2X11_GPIO67_I2C3_SDA		67
#define STA2X11_GPIO68_MSP0_RCK		68
#define STA2X11_GPIO69_MSP0_RXD		69
#define STA2X11_GPIO70_MSP0_RFS		70
#define STA2X11_GPIO71_MSP0_TCK		71
#define STA2X11_GPIO72_MSP0_TXD		72
#define STA2X11_GPIO73_MSP0_TFS		73
#define STA2X11_GPIO74_MSP0_SCK		74
#define STA2X11_GPIO75_MSP1_CK		75
#define STA2X11_GPIO76_MSP1_RXD		76
#define STA2X11_GPIO77_MSP1_FS		77
#define STA2X11_GPIO78_MSP1_TXD		78
#define STA2X11_GPIO79_MSP2_CK		79
#define STA2X11_GPIO80_MSP2_RXD		80
#define STA2X11_GPIO81_MSP2_FS		81
#define STA2X11_GPIO82_MSP2_TXD		82
#define STA2X11_GPIO83_MSP3_CK		83
#define STA2X11_GPIO84_MSP3_RXD		84
#define STA2X11_GPIO85_MSP3_FS		85
#define STA2X11_GPIO86_MSP3_TXD		86
#define STA2X11_GPIO87_MSP4_CK		87
#define STA2X11_GPIO88_MSP4_RXD		88
#define STA2X11_GPIO89_MSP4_FS		89
#define STA2X11_GPIO90_MSP4_TXD		90
#define STA2X11_GPIO91_MSP5_CK		91
#define STA2X11_GPIO92_MSP5_RXD		92
#define STA2X11_GPIO93_MSP5_FS		93
#define STA2X11_GPIO94_MSP5_TXD		94
#define STA2X11_GPIO95_SDIO3_DAT3	95
#define STA2X11_GPIO96_SDIO3_DAT2	96
#define STA2X11_GPIO97_SDIO3_DAT1	97
#define STA2X11_GPIO98_SDIO3_DAT0	98
#define STA2X11_GPIO99_SDIO3_CLK	99
#define STA2X11_GPIO100_SDIO3_CMD	100
#define STA2X11_GPIO101			101
#define STA2X11_GPIO102			102
#define STA2X11_GPIO103			103
#define STA2X11_GPIO104			104
#define STA2X11_GPIO105_SDIO2_DAT3	105
#define STA2X11_GPIO106_SDIO2_DAT2	106
#define STA2X11_GPIO107_SDIO2_DAT1	107
#define STA2X11_GPIO108_SDIO2_DAT0	108
#define STA2X11_GPIO109_SDIO2_CLK	109
#define STA2X11_GPIO110_SDIO2_CMD	110
#define STA2X11_GPIO111			111
#define STA2X11_GPIO112			112
#define STA2X11_GPIO113			113
#define STA2X11_GPIO114			114
#define STA2X11_GPIO115_SDIO1_DAT3	115
#define STA2X11_GPIO116_SDIO1_DAT2	116
#define STA2X11_GPIO117_SDIO1_DAT1	117
#define STA2X11_GPIO118_SDIO1_DAT0	118
#define STA2X11_GPIO119_SDIO1_CLK	119
#define STA2X11_GPIO120_SDIO1_CMD	120
#define STA2X11_GPIO121			121
#define STA2X11_GPIO122			122
#define STA2X11_GPIO123			123
#define STA2X11_GPIO124			124
#define STA2X11_GPIO125_UART2_TXD	125
#define STA2X11_GPIO126_UART2_RXD	126
#define STA2X11_GPIO127_UART3_TXD	127

/*
 * The APB bridge has its own registers, needed by our users as well.
 * They are accessed with the following read/mask/write function.
 */
static inline u32
sta2x11_apbreg_mask(struct pci_dev *pdev, u32 reg, u32 mask, u32 val)
{
	return __sta2x11_mfd_mask(pdev, reg, mask, val, sta2x11_apbreg);
}

/* CAN and MLB */
#define APBREG_BSR	0x00	/* Bridge Status Reg */
#define APBREG_PAER	0x08	/* Peripherals Address Error Reg */
#define APBREG_PWAC	0x20	/* Peripheral Write Access Control reg */
#define APBREG_PRAC	0x40	/* Peripheral Read Access Control reg */
#define APBREG_PCG	0x60	/* Peripheral Clock Gating Reg */
#define APBREG_PUR	0x80	/* Peripheral Under Reset Reg */
#define APBREG_EMU_PCG	0xA0	/* Emulator Peripheral Clock Gating Reg */

#define APBREG_CAN	(1 << 1)
#define APBREG_MLB	(1 << 3)

/* SARAC */
#define APBREG_BSR_SARAC     0x100 /* Bridge Status Reg */
#define APBREG_PAER_SARAC    0x108 /* Peripherals Address Error Reg */
#define APBREG_PWAC_SARAC    0x120 /* Peripheral Write Access Control reg */
#define APBREG_PRAC_SARAC    0x140 /* Peripheral Read Access Control reg */
#define APBREG_PCG_SARAC     0x160 /* Peripheral Clock Gating Reg */
#define APBREG_PUR_SARAC     0x180 /* Peripheral Under Reset Reg */
#define APBREG_EMU_PCG_SARAC 0x1A0 /* Emulator Peripheral Clock Gating Reg */

#define APBREG_SARAC	(1 << 2)

/*
 * The system controller has its own registers. Some of these are accessed
 * by out users as well, using the following read/mask/write/function
 */
static inline
u32 sta2x11_sctl_mask(struct pci_dev *pdev, u32 reg, u32 mask, u32 val)
{
	return __sta2x11_mfd_mask(pdev, reg, mask, val, sta2x11_sctl);
}

#define SCTL_SCCTL		0x00	/* System controller control register */
#define SCTL_ARMCFG		0x04	/* ARM configuration register */
#define SCTL_SCPLLCTL		0x08	/* PLL control status register */
#define SCTL_SCPLLFCTRL		0x0c	/* PLL frequency control register */
#define SCTL_SCRESFRACT		0x10	/* PLL fractional input register */
#define SCTL_SCRESCTRL1		0x14	/* Peripheral reset control 1 */
#define SCTL_SCRESXTRL2		0x18	/* Peripheral reset control 2 */
#define SCTL_SCPEREN0		0x1c	/* Peripheral clock enable register 0 */
#define SCTL_SCPEREN1		0x20	/* Peripheral clock enable register 1 */
#define SCTL_SCPEREN2		0x24	/* Peripheral clock enable register 2 */
#define SCTL_SCGRST		0x28	/* Peripheral global reset */
#define SCTL_SCPCIECSBRST       0x2c    /* PCIe PAB CSB reset status register */
#define SCTL_SCPCIPMCR1		0x30	/* PCI power management control 1 */
#define SCTL_SCPCIPMCR2		0x34	/* PCI power management control 2 */
#define SCTL_SCPCIPMSR1		0x38	/* PCI power management status 1 */
#define SCTL_SCPCIPMSR2		0x3c	/* PCI power management status 2 */
#define SCTL_SCPCIPMSR3		0x40	/* PCI power management status 3 */
#define SCTL_SCINTREN		0x44	/* Interrupt enable */
#define SCTL_SCRISR		0x48	/* RAW interrupt status */
#define SCTL_SCCLKSTAT0		0x4c	/* Peripheral clocks status 0 */
#define SCTL_SCCLKSTAT1		0x50	/* Peripheral clocks status 1 */
#define SCTL_SCCLKSTAT2		0x54	/* Peripheral clocks status 2 */
#define SCTL_SCRSTSTA		0x58	/* Reset status register */

#define SCTL_SCRESCTRL1_USB_PHY_POR	(1 << 0)
#define SCTL_SCRESCTRL1_USB_OTG	(1 << 1)
#define SCTL_SCRESCTRL1_USB_HRST	(1 << 2)
#define SCTL_SCRESCTRL1_USB_PHY_HOST	(1 << 3)
#define SCTL_SCRESCTRL1_SATAII	(1 << 4)
#define SCTL_SCRESCTRL1_VIP		(1 << 5)
#define SCTL_SCRESCTRL1_PER_MMC0	(1 << 6)
#define SCTL_SCRESCTRL1_PER_MMC1	(1 << 7)
#define SCTL_SCRESCTRL1_PER_GPIO0	(1 << 8)
#define SCTL_SCRESCTRL1_PER_GPIO1	(1 << 9)
#define SCTL_SCRESCTRL1_PER_GPIO2	(1 << 10)
#define SCTL_SCRESCTRL1_PER_GPIO3	(1 << 11)
#define SCTL_SCRESCTRL1_PER_MTU0	(1 << 12)
#define SCTL_SCRESCTRL1_KER_SPI0	(1 << 13)
#define SCTL_SCRESCTRL1_KER_SPI1	(1 << 14)
#define SCTL_SCRESCTRL1_KER_SPI2	(1 << 15)
#define SCTL_SCRESCTRL1_KER_MCI0	(1 << 16)
#define SCTL_SCRESCTRL1_KER_MCI1	(1 << 17)
#define SCTL_SCRESCTRL1_PRE_HSI2C0	(1 << 18)
#define SCTL_SCRESCTRL1_PER_HSI2C1	(1 << 19)
#define SCTL_SCRESCTRL1_PER_HSI2C2	(1 << 20)
#define SCTL_SCRESCTRL1_PER_HSI2C3	(1 << 21)
#define SCTL_SCRESCTRL1_PER_MSP0	(1 << 22)
#define SCTL_SCRESCTRL1_PER_MSP1	(1 << 23)
#define SCTL_SCRESCTRL1_PER_MSP2	(1 << 24)
#define SCTL_SCRESCTRL1_PER_MSP3	(1 << 25)
#define SCTL_SCRESCTRL1_PER_MSP4	(1 << 26)
#define SCTL_SCRESCTRL1_PER_MSP5	(1 << 27)
#define SCTL_SCRESCTRL1_PER_MMC	(1 << 28)
#define SCTL_SCRESCTRL1_KER_MSP0	(1 << 29)
#define SCTL_SCRESCTRL1_KER_MSP1	(1 << 30)
#define SCTL_SCRESCTRL1_KER_MSP2	(1 << 31)

#define SCTL_SCPEREN0_UART0		(1 << 0)
#define SCTL_SCPEREN0_UART1		(1 << 1)
#define SCTL_SCPEREN0_UART2		(1 << 2)
#define SCTL_SCPEREN0_UART3		(1 << 3)
#define SCTL_SCPEREN0_MSP0		(1 << 4)
#define SCTL_SCPEREN0_MSP1		(1 << 5)
#define SCTL_SCPEREN0_MSP2		(1 << 6)
#define SCTL_SCPEREN0_MSP3		(1 << 7)
#define SCTL_SCPEREN0_MSP4		(1 << 8)
#define SCTL_SCPEREN0_MSP5		(1 << 9)
#define SCTL_SCPEREN0_SPI0		(1 << 10)
#define SCTL_SCPEREN0_SPI1		(1 << 11)
#define SCTL_SCPEREN0_SPI2		(1 << 12)
#define SCTL_SCPEREN0_I2C0		(1 << 13)
#define SCTL_SCPEREN0_I2C1		(1 << 14)
#define SCTL_SCPEREN0_I2C2		(1 << 15)
#define SCTL_SCPEREN0_I2C3		(1 << 16)
#define SCTL_SCPEREN0_SVDO_LVDS		(1 << 17)
#define SCTL_SCPEREN0_USB_HOST		(1 << 18)
#define SCTL_SCPEREN0_USB_OTG		(1 << 19)
#define SCTL_SCPEREN0_MCI0		(1 << 20)
#define SCTL_SCPEREN0_MCI1		(1 << 21)
#define SCTL_SCPEREN0_MCI2		(1 << 22)
#define SCTL_SCPEREN0_MCI3		(1 << 23)
#define SCTL_SCPEREN0_SATA		(1 << 24)
#define SCTL_SCPEREN0_ETHERNET		(1 << 25)
#define SCTL_SCPEREN0_VIC		(1 << 26)
#define SCTL_SCPEREN0_DMA_AUDIO		(1 << 27)
#define SCTL_SCPEREN0_DMA_SOC		(1 << 28)
#define SCTL_SCPEREN0_RAM		(1 << 29)
#define SCTL_SCPEREN0_VIP		(1 << 30)
#define SCTL_SCPEREN0_ARM		(1 << 31)

#define SCTL_SCPEREN1_UART0		(1 << 0)
#define SCTL_SCPEREN1_UART1		(1 << 1)
#define SCTL_SCPEREN1_UART2		(1 << 2)
#define SCTL_SCPEREN1_UART3		(1 << 3)
#define SCTL_SCPEREN1_MSP0		(1 << 4)
#define SCTL_SCPEREN1_MSP1		(1 << 5)
#define SCTL_SCPEREN1_MSP2		(1 << 6)
#define SCTL_SCPEREN1_MSP3		(1 << 7)
#define SCTL_SCPEREN1_MSP4		(1 << 8)
#define SCTL_SCPEREN1_MSP5		(1 << 9)
#define SCTL_SCPEREN1_SPI0		(1 << 10)
#define SCTL_SCPEREN1_SPI1		(1 << 11)
#define SCTL_SCPEREN1_SPI2		(1 << 12)
#define SCTL_SCPEREN1_I2C0		(1 << 13)
#define SCTL_SCPEREN1_I2C1		(1 << 14)
#define SCTL_SCPEREN1_I2C2		(1 << 15)
#define SCTL_SCPEREN1_I2C3		(1 << 16)
#define SCTL_SCPEREN1_USB_PHY		(1 << 17)

/*
 * APB-SOC registers
 */
static inline
u32 sta2x11_apb_soc_regs_mask(struct pci_dev *pdev, u32 reg, u32 mask, u32 val)
{
	return __sta2x11_mfd_mask(pdev, reg, mask, val, sta2x11_apb_soc_regs);
}

#define PCIE_EP1_FUNC3_0_INTR_REG	0x000
#define PCIE_EP1_FUNC7_4_INTR_REG	0x004
#define PCIE_EP2_FUNC3_0_INTR_REG	0x008
#define PCIE_EP2_FUNC7_4_INTR_REG	0x00c
#define PCIE_EP3_FUNC3_0_INTR_REG	0x010
#define PCIE_EP3_FUNC7_4_INTR_REG	0x014
#define PCIE_EP4_FUNC3_0_INTR_REG	0x018
#define PCIE_EP4_FUNC7_4_INTR_REG	0x01c
#define PCIE_INTR_ENABLE0_REG		0x020
#define PCIE_INTR_ENABLE1_REG		0x024
#define PCIE_EP1_FUNC_TC_REG		0x028
#define PCIE_EP2_FUNC_TC_REG		0x02c
#define PCIE_EP3_FUNC_TC_REG		0x030
#define PCIE_EP4_FUNC_TC_REG		0x034
#define PCIE_EP1_FUNC_F_REG		0x038
#define PCIE_EP2_FUNC_F_REG		0x03c
#define PCIE_EP3_FUNC_F_REG		0x040
#define PCIE_EP4_FUNC_F_REG		0x044
#define PCIE_PAB_AMBA_SW_RST_REG	0x048
#define PCIE_PM_STATUS_0_PORT_0_4	0x04c
#define PCIE_PM_STATUS_7_0_EP1		0x050
#define PCIE_PM_STATUS_7_0_EP2		0x054
#define PCIE_PM_STATUS_7_0_EP3		0x058
#define PCIE_PM_STATUS_7_0_EP4		0x05c
#define PCIE_DEV_ID_0_EP1_REG		0x060
#define PCIE_CC_REV_ID_0_EP1_REG	0x064
#define PCIE_DEV_ID_1_EP1_REG		0x068
#define PCIE_CC_REV_ID_1_EP1_REG	0x06c
#define PCIE_DEV_ID_2_EP1_REG		0x070
#define PCIE_CC_REV_ID_2_EP1_REG	0x074
#define PCIE_DEV_ID_3_EP1_REG		0x078
#define PCIE_CC_REV_ID_3_EP1_REG	0x07c
#define PCIE_DEV_ID_4_EP1_REG		0x080
#define PCIE_CC_REV_ID_4_EP1_REG	0x084
#define PCIE_DEV_ID_5_EP1_REG		0x088
#define PCIE_CC_REV_ID_5_EP1_REG	0x08c
#define PCIE_DEV_ID_6_EP1_REG		0x090
#define PCIE_CC_REV_ID_6_EP1_REG	0x094
#define PCIE_DEV_ID_7_EP1_REG		0x098
#define PCIE_CC_REV_ID_7_EP1_REG	0x09c
#define PCIE_DEV_ID_0_EP2_REG		0x0a0
#define PCIE_CC_REV_ID_0_EP2_REG	0x0a4
#define PCIE_DEV_ID_1_EP2_REG		0x0a8
#define PCIE_CC_REV_ID_1_EP2_REG	0x0ac
#define PCIE_DEV_ID_2_EP2_REG		0x0b0
#define PCIE_CC_REV_ID_2_EP2_REG	0x0b4
#define PCIE_DEV_ID_3_EP2_REG		0x0b8
#define PCIE_CC_REV_ID_3_EP2_REG	0x0bc
#define PCIE_DEV_ID_4_EP2_REG		0x0c0
#define PCIE_CC_REV_ID_4_EP2_REG	0x0c4
#define PCIE_DEV_ID_5_EP2_REG		0x0c8
#define PCIE_CC_REV_ID_5_EP2_REG	0x0cc
#define PCIE_DEV_ID_6_EP2_REG		0x0d0
#define PCIE_CC_REV_ID_6_EP2_REG	0x0d4
#define PCIE_DEV_ID_7_EP2_REG		0x0d8
#define PCIE_CC_REV_ID_7_EP2_REG	0x0dC
#define PCIE_DEV_ID_0_EP3_REG		0x0e0
#define PCIE_CC_REV_ID_0_EP3_REG	0x0e4
#define PCIE_DEV_ID_1_EP3_REG		0x0e8
#define PCIE_CC_REV_ID_1_EP3_REG	0x0ec
#define PCIE_DEV_ID_2_EP3_REG		0x0f0
#define PCIE_CC_REV_ID_2_EP3_REG	0x0f4
#define PCIE_DEV_ID_3_EP3_REG		0x0f8
#define PCIE_CC_REV_ID_3_EP3_REG	0x0fc
#define PCIE_DEV_ID_4_EP3_REG		0x100
#define PCIE_CC_REV_ID_4_EP3_REG	0x104
#define PCIE_DEV_ID_5_EP3_REG		0x108
#define PCIE_CC_REV_ID_5_EP3_REG	0x10c
#define PCIE_DEV_ID_6_EP3_REG		0x110
#define PCIE_CC_REV_ID_6_EP3_REG	0x114
#define PCIE_DEV_ID_7_EP3_REG		0x118
#define PCIE_CC_REV_ID_7_EP3_REG	0x11c
#define PCIE_DEV_ID_0_EP4_REG		0x120
#define PCIE_CC_REV_ID_0_EP4_REG	0x124
#define PCIE_DEV_ID_1_EP4_REG		0x128
#define PCIE_CC_REV_ID_1_EP4_REG	0x12c
#define PCIE_DEV_ID_2_EP4_REG		0x130
#define PCIE_CC_REV_ID_2_EP4_REG	0x134
#define PCIE_DEV_ID_3_EP4_REG		0x138
#define PCIE_CC_REV_ID_3_EP4_REG	0x13c
#define PCIE_DEV_ID_4_EP4_REG		0x140
#define PCIE_CC_REV_ID_4_EP4_REG	0x144
#define PCIE_DEV_ID_5_EP4_REG		0x148
#define PCIE_CC_REV_ID_5_EP4_REG	0x14c
#define PCIE_DEV_ID_6_EP4_REG		0x150
#define PCIE_CC_REV_ID_6_EP4_REG	0x154
#define PCIE_DEV_ID_7_EP4_REG		0x158
#define PCIE_CC_REV_ID_7_EP4_REG	0x15c
#define PCIE_SUBSYS_VEN_ID_REG		0x160
#define PCIE_COMMON_CLOCK_CONFIG_0_4_0	0x164
#define PCIE_MIPHYP_SSC_EN_REG		0x168
#define PCIE_MIPHYP_ADDR_REG		0x16c
#define PCIE_L1_ASPM_READY_REG		0x170
#define PCIE_EXT_CFG_RDY_REG		0x174
#define PCIE_SoC_INT_ROUTER_STATUS0_REG 0x178
#define PCIE_SoC_INT_ROUTER_STATUS1_REG 0x17c
#define PCIE_SoC_INT_ROUTER_STATUS2_REG 0x180
#define PCIE_SoC_INT_ROUTER_STATUS3_REG 0x184
#define DMA_IP_CTRL_REG			0x324
#define DISP_BRIDGE_PU_PD_CTRL_REG	0x328
#define VIP_PU_PD_CTRL_REG		0x32c
#define USB_MLB_PU_PD_CTRL_REG		0x330
#define SDIO_PU_PD_MISCFUNC_CTRL_REG1	0x334
#define SDIO_PU_PD_MISCFUNC_CTRL_REG2	0x338
#define UART_PU_PD_CTRL_REG		0x33c
#define ARM_Lock			0x340
#define SYS_IO_CHAR_REG1		0x344
#define SYS_IO_CHAR_REG2		0x348
#define SATA_CORE_ID_REG		0x34c
#define SATA_CTRL_REG			0x350
#define I2C_HSFIX_MISC_REG		0x354
#define SPARE2_RESERVED			0x358
#define SPARE3_RESERVED			0x35c
#define MASTER_LOCK_REG			0x368
#define SYSTEM_CONFIG_STATUS_REG	0x36c
#define MSP_CLK_CTRL_REG		0x39c
#define COMPENSATION_REG1		0x3c4
#define COMPENSATION_REG2		0x3c8
#define COMPENSATION_REG3		0x3cc
#define TEST_CTL_REG			0x3d0

/*
 * SECR (OTP) registers
 */
#define STA2X11_SECR_CR			0x00
#define STA2X11_SECR_FVR0		0x10
#define STA2X11_SECR_FVR1		0x14

extern int sta2x11_mfd_get_regs_data(struct platform_device *pdev,
				     enum sta2x11_mfd_plat_dev index,
				     void __iomem **regs,
				     spinlock_t **lock);

#endif /* __STA2X11_MFD_H */
