/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ASM_ARCH_OMAP_IO_H
#define __ASM_ARCH_OMAP_IO_H

#ifndef __ASSEMBLER__
#include <linux/types.h>

#ifdef CONFIG_ARCH_OMAP1
/*
 * NOTE: Please use ioremap + __raw_read/write where possible instead of these
 */
extern u8 omap_readb(u32 pa);
extern u16 omap_readw(u32 pa);
extern u32 omap_readl(u32 pa);
extern void omap_writeb(u8 v, u32 pa);
extern void omap_writew(u16 v, u32 pa);
extern void omap_writel(u32 v, u32 pa);
#elif defined(CONFIG_COMPILE_TEST)
static inline u8 omap_readb(u32 pa)  { return 0; }
static inline u16 omap_readw(u32 pa) { return 0; }
static inline u32 omap_readl(u32 pa) { return 0; }
static inline void omap_writeb(u8 v, u32 pa)   { }
static inline void omap_writew(u16 v, u32 pa)  { }
static inline void omap_writel(u32 v, u32 pa)  { }
#endif
#endif

/*
 * ----------------------------------------------------------------------------
 * System control registers
 * ----------------------------------------------------------------------------
 */
#define MOD_CONF_CTRL_0		0xfffe1080
#define MOD_CONF_CTRL_1		0xfffe1110

/*
 * ---------------------------------------------------------------------------
 * UPLD
 * ---------------------------------------------------------------------------
 */
#define ULPD_REG_BASE		(0xfffe0800)
#define ULPD_IT_STATUS		(ULPD_REG_BASE + 0x14)
#define ULPD_SETUP_ANALOG_CELL_3	(ULPD_REG_BASE + 0x24)
#define ULPD_CLOCK_CTRL		(ULPD_REG_BASE + 0x30)
#	define DIS_USB_PVCI_CLK		(1 << 5)	/* no USB/FAC synch */
#	define USB_MCLK_EN		(1 << 4)	/* enable W4_USB_CLKO */
#define ULPD_SOFT_REQ		(ULPD_REG_BASE + 0x34)
#	define SOFT_UDC_REQ		(1 << 4)
#	define SOFT_USB_CLK_REQ		(1 << 3)
#	define SOFT_DPLL_REQ		(1 << 0)
#define ULPD_DPLL_CTRL		(ULPD_REG_BASE + 0x3c)
#define ULPD_STATUS_REQ		(ULPD_REG_BASE + 0x40)
#define ULPD_APLL_CTRL		(ULPD_REG_BASE + 0x4c)
#define ULPD_POWER_CTRL		(ULPD_REG_BASE + 0x50)
#define ULPD_SOFT_DISABLE_REQ_REG	(ULPD_REG_BASE + 0x68)
#	define DIS_MMC2_DPLL_REQ	(1 << 11)
#	define DIS_MMC1_DPLL_REQ	(1 << 10)
#	define DIS_UART3_DPLL_REQ	(1 << 9)
#	define DIS_UART2_DPLL_REQ	(1 << 8)
#	define DIS_UART1_DPLL_REQ	(1 << 7)
#	define DIS_USB_HOST_DPLL_REQ	(1 << 6)
#define ULPD_SDW_CLK_DIV_CTRL_SEL	(ULPD_REG_BASE + 0x74)
#define ULPD_CAM_CLK_CTRL	(ULPD_REG_BASE + 0x7c)

/*
 * ----------------------------------------------------------------------------
 * Clocks
 * ----------------------------------------------------------------------------
 */
#define CLKGEN_REG_BASE		(0xfffece00)
#define ARM_CKCTL		(CLKGEN_REG_BASE + 0x0)
#define ARM_IDLECT1		(CLKGEN_REG_BASE + 0x4)
#define ARM_IDLECT2		(CLKGEN_REG_BASE + 0x8)
#define ARM_EWUPCT		(CLKGEN_REG_BASE + 0xC)
#define ARM_RSTCT1		(CLKGEN_REG_BASE + 0x10)
#define ARM_RSTCT2		(CLKGEN_REG_BASE + 0x14)
#define ARM_SYSST		(CLKGEN_REG_BASE + 0x18)
#define ARM_IDLECT3		(CLKGEN_REG_BASE + 0x24)

#define CK_RATEF		1
#define CK_IDLEF		2
#define CK_ENABLEF		4
#define CK_SELECTF		8
#define SETARM_IDLE_SHIFT

/* DPLL control registers */
#define DPLL_CTL		(0xfffecf00)

/* DSP clock control. Must use __raw_readw() and __raw_writew() with these */
#define DSP_CONFIG_REG_BASE     IOMEM(0xe1008000)
#define DSP_CKCTL		(DSP_CONFIG_REG_BASE + 0x0)
#define DSP_IDLECT1		(DSP_CONFIG_REG_BASE + 0x4)
#define DSP_IDLECT2		(DSP_CONFIG_REG_BASE + 0x8)
#define DSP_RSTCT2		(DSP_CONFIG_REG_BASE + 0x14)

/*
 * ----------------------------------------------------------------------------
 * Pulse-Width Light
 * ----------------------------------------------------------------------------
 */
#define OMAP_PWL_BASE			0xfffb5800
#define OMAP_PWL_ENABLE			(OMAP_PWL_BASE + 0x00)
#define OMAP_PWL_CLK_ENABLE		(OMAP_PWL_BASE + 0x04)

/*
 * ----------------------------------------------------------------------------
 * Pin multiplexing registers
 * ----------------------------------------------------------------------------
 */
#define FUNC_MUX_CTRL_0		0xfffe1000
#define FUNC_MUX_CTRL_1		0xfffe1004
#define FUNC_MUX_CTRL_2		0xfffe1008
#define COMP_MODE_CTRL_0	0xfffe100c
#define FUNC_MUX_CTRL_3		0xfffe1010
#define FUNC_MUX_CTRL_4		0xfffe1014
#define FUNC_MUX_CTRL_5		0xfffe1018
#define FUNC_MUX_CTRL_6		0xfffe101C
#define FUNC_MUX_CTRL_7		0xfffe1020
#define FUNC_MUX_CTRL_8		0xfffe1024
#define FUNC_MUX_CTRL_9		0xfffe1028
#define FUNC_MUX_CTRL_A		0xfffe102C
#define FUNC_MUX_CTRL_B		0xfffe1030
#define FUNC_MUX_CTRL_C		0xfffe1034
#define FUNC_MUX_CTRL_D		0xfffe1038
#define PULL_DWN_CTRL_0		0xfffe1040
#define PULL_DWN_CTRL_1		0xfffe1044
#define PULL_DWN_CTRL_2		0xfffe1048
#define PULL_DWN_CTRL_3		0xfffe104c
#define PULL_DWN_CTRL_4		0xfffe10ac

/* OMAP-1610 specific multiplexing registers */
#define FUNC_MUX_CTRL_E		0xfffe1090
#define FUNC_MUX_CTRL_F		0xfffe1094
#define FUNC_MUX_CTRL_10	0xfffe1098
#define FUNC_MUX_CTRL_11	0xfffe109c
#define FUNC_MUX_CTRL_12	0xfffe10a0
#define PU_PD_SEL_0		0xfffe10b4
#define PU_PD_SEL_1		0xfffe10b8
#define PU_PD_SEL_2		0xfffe10bc
#define PU_PD_SEL_3		0xfffe10c0
#define PU_PD_SEL_4		0xfffe10c4

#endif
