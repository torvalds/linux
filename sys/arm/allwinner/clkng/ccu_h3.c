/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017,2018 Emmanuel Vadot <manu@freebsd.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <machine/bus.h>

#include <dev/fdt/simplebus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>

#include <dev/extres/clk/clk_div.h>
#include <dev/extres/clk/clk_fixed.h>
#include <dev/extres/clk/clk_mux.h>

#if defined(__aarch64__)
#include "opt_soc.h"
#endif

#include <arm/allwinner/clkng/aw_ccung.h>

#include <gnu/dts/include/dt-bindings/clock/sun8i-h3-ccu.h>
#include <gnu/dts/include/dt-bindings/reset/sun8i-h3-ccu.h>

/* Non-exported resets */
#define	RST_BUS_SCR		53

/* Non-exported clocks */
#define	CLK_PLL_CPUX		0
#define	CLK_PLL_AUDIO_BASE	1
#define	CLK_PLL_AUDIO		2
#define	CLK_PLL_AUDIO_2X	3
#define	CLK_PLL_AUDIO_4X	4
#define	CLK_PLL_AUDIO_8X	5
#define	CLK_PLL_VIDEO		6
#define	CLK_PLL_VE		7
#define	CLK_PLL_DDR		8
#define	CLK_PLL_PERIPH0_2X	10
#define	CLK_PLL_GPU		11
#define	CLK_PLL_PERIPH1		12
#define	CLK_PLL_DE		13

#define	CLK_AXI			15
#define	CLK_AHB1		16
#define	CLK_APB1		17
#define	CLK_APB2		18
#define	CLK_AHB2		19

#define	CLK_BUS_SCR		66

#define	CLK_USBPHY0		88
#define	CLK_USBPHY1		89
#define	CLK_USBPHY2		90
#define	CLK_USBPHY3		91
#define	CLK_USBOHCI0		92
#define	CLK_USBOHCI1		93
#define	CLK_USBOHCI2		94
#define	CLK_USBOHCI3		95
#define	CLK_DRAM		96

#define	CLK_MBUS		113

static struct aw_ccung_reset h3_ccu_resets[] = {
	CCU_RESET(RST_USB_PHY0, 0xcc, 0)
	CCU_RESET(RST_USB_PHY1, 0xcc, 1)
	CCU_RESET(RST_USB_PHY2, 0xcc, 2)
	CCU_RESET(RST_USB_PHY3, 0xcc, 3)

	CCU_RESET(RST_MBUS, 0xfc, 31)

	CCU_RESET(RST_BUS_CE, 0x2c0, 5)
	CCU_RESET(RST_BUS_DMA, 0x2c0, 6)
	CCU_RESET(RST_BUS_MMC0, 0x2c0, 8)
	CCU_RESET(RST_BUS_MMC1, 0x2c0, 9)
	CCU_RESET(RST_BUS_MMC2, 0x2c0, 10)
	CCU_RESET(RST_BUS_NAND, 0x2c0, 13)
	CCU_RESET(RST_BUS_DRAM, 0x2c0, 14)
	CCU_RESET(RST_BUS_EMAC, 0x2c0, 17)
	CCU_RESET(RST_BUS_TS, 0x2c0, 18)
	CCU_RESET(RST_BUS_HSTIMER, 0x2c0, 19)
	CCU_RESET(RST_BUS_SPI0, 0x2c0, 20)
	CCU_RESET(RST_BUS_SPI1, 0x2c0, 21)
	CCU_RESET(RST_BUS_OTG, 0x2c0, 23)
	CCU_RESET(RST_BUS_EHCI0, 0x2c0, 24)
	CCU_RESET(RST_BUS_EHCI1, 0x2c0, 25)
	CCU_RESET(RST_BUS_EHCI2, 0x2c0, 26)
	CCU_RESET(RST_BUS_EHCI3, 0x2c0, 27)
	CCU_RESET(RST_BUS_OHCI0, 0x2c0, 28)
	CCU_RESET(RST_BUS_OHCI1, 0x2c0, 29)
	CCU_RESET(RST_BUS_OHCI2, 0x2c0, 30)
	CCU_RESET(RST_BUS_OHCI3, 0x2c0, 31)

	CCU_RESET(RST_BUS_VE, 0x2c4, 0)
	CCU_RESET(RST_BUS_TCON0, 0x2c4, 3)
	CCU_RESET(RST_BUS_TCON1, 0x2c4, 4)
	CCU_RESET(RST_BUS_DEINTERLACE, 0x2c4, 5)
	CCU_RESET(RST_BUS_CSI, 0x2c4, 8)
	CCU_RESET(RST_BUS_TVE, 0x2c4, 9)
	CCU_RESET(RST_BUS_HDMI0, 0x2c4, 10)
	CCU_RESET(RST_BUS_HDMI1, 0x2c4, 11)
	CCU_RESET(RST_BUS_DE, 0x2c4, 12)
	CCU_RESET(RST_BUS_GPU, 0x2c4, 20)
	CCU_RESET(RST_BUS_MSGBOX, 0x2c4, 21)
	CCU_RESET(RST_BUS_SPINLOCK, 0x2c4, 22)
	CCU_RESET(RST_BUS_DBG, 0x2c4, 31)

	CCU_RESET(RST_BUS_EPHY, 0x2c8, 2)

	CCU_RESET(RST_BUS_CODEC, 0x2d0, 0)
	CCU_RESET(RST_BUS_SPDIF, 0x2d0, 1)
	CCU_RESET(RST_BUS_THS, 0x2d0, 8)
	CCU_RESET(RST_BUS_I2S0, 0x2d0, 12)
	CCU_RESET(RST_BUS_I2S1, 0x2d0, 13)
	CCU_RESET(RST_BUS_I2S2, 0x2d0, 14)

	CCU_RESET(RST_BUS_I2C0, 0x2d8, 0)
	CCU_RESET(RST_BUS_I2C1, 0x2d8, 1)
	CCU_RESET(RST_BUS_I2C2, 0x2d8, 2)
	CCU_RESET(RST_BUS_UART0, 0x2d8, 16)
	CCU_RESET(RST_BUS_UART1, 0x2d8, 17)
	CCU_RESET(RST_BUS_UART2, 0x2d8, 18)
	CCU_RESET(RST_BUS_UART3, 0x2d8, 19)
	CCU_RESET(RST_BUS_SCR, 0x2d8, 20)
};

static struct aw_ccung_gate h3_ccu_gates[] = {
	CCU_GATE(CLK_BUS_CE, "bus-ce", "ahb1", 0x60, 5)
	CCU_GATE(CLK_BUS_DMA, "bus-dma", "ahb1", 0x60, 6)
	CCU_GATE(CLK_BUS_MMC0, "bus-mmc0", "ahb1", 0x60, 8)
	CCU_GATE(CLK_BUS_MMC1, "bus-mmc1", "ahb1", 0x60, 9)
	CCU_GATE(CLK_BUS_MMC2, "bus-mmc2", "ahb1", 0x60, 10)
	CCU_GATE(CLK_BUS_NAND, "bus-nand", "ahb1", 0x60, 13)
	CCU_GATE(CLK_BUS_DRAM, "bus-dram", "ahb1", 0x60, 14)
	CCU_GATE(CLK_BUS_EMAC, "bus-emac", "ahb2", 0x60, 17)
	CCU_GATE(CLK_BUS_TS, "bus-ts", "ahb1", 0x60, 18)
	CCU_GATE(CLK_BUS_HSTIMER, "bus-hstimer", "ahb1", 0x60, 19)
	CCU_GATE(CLK_BUS_SPI0, "bus-spi0", "ahb1", 0x60, 20)
	CCU_GATE(CLK_BUS_SPI1, "bus-spi1", "ahb1", 0x60, 21)
	CCU_GATE(CLK_BUS_OTG, "bus-otg", "ahb1", 0x60, 23)
	CCU_GATE(CLK_BUS_EHCI0, "bus-ehci0", "ahb1", 0x60, 24)
	CCU_GATE(CLK_BUS_EHCI1, "bus-ehci1", "ahb2", 0x60, 25)
	CCU_GATE(CLK_BUS_EHCI2, "bus-ehci2", "ahb2", 0x60, 26)
	CCU_GATE(CLK_BUS_EHCI3, "bus-ehci3", "ahb2", 0x60, 27)
	CCU_GATE(CLK_BUS_OHCI0, "bus-ohci0", "ahb1", 0x60, 28)
	CCU_GATE(CLK_BUS_OHCI1, "bus-ohci1", "ahb2", 0x60, 29)
	CCU_GATE(CLK_BUS_OHCI2, "bus-ohci2", "ahb2", 0x60, 30)
	CCU_GATE(CLK_BUS_OHCI3, "bus-ohci3", "ahb2", 0x60, 31)

	CCU_GATE(CLK_BUS_VE, "bus-ve", "ahb1", 0x64, 0)
	CCU_GATE(CLK_BUS_TCON0, "bus-tcon0", "ahb1", 0x64, 3)
	CCU_GATE(CLK_BUS_TCON1, "bus-tcon1", "ahb1", 0x64, 4)
	CCU_GATE(CLK_BUS_DEINTERLACE, "bus-deinterlace", "ahb1", 0x64, 5)
	CCU_GATE(CLK_BUS_CSI, "bus-csi", "ahb1", 0x64, 8)
	CCU_GATE(CLK_BUS_TVE, "bus-tve", "ahb1", 0x64, 9)
	CCU_GATE(CLK_BUS_HDMI, "bus-hdmi", "ahb1", 0x64, 11)
	CCU_GATE(CLK_BUS_DE, "bus-de", "ahb1", 0x64, 12)
	CCU_GATE(CLK_BUS_GPU, "bus-gpu", "ahb1", 0x64, 20)
	CCU_GATE(CLK_BUS_MSGBOX, "bus-msgbox", "ahb1", 0x64, 21)
	CCU_GATE(CLK_BUS_SPINLOCK, "bus-spinlock", "ahb1", 0x64, 22)

	CCU_GATE(CLK_BUS_CODEC, "bus-codec", "apb1", 0x68, 0)
	CCU_GATE(CLK_BUS_SPDIF, "bus-spdif", "apb1", 0x68, 1)
	CCU_GATE(CLK_BUS_PIO, "bus-pio", "apb1", 0x68, 5)
	CCU_GATE(CLK_BUS_THS, "bus-ths", "apb1", 0x68, 8)
	CCU_GATE(CLK_BUS_I2S0, "bus-i2s0", "apb1", 0x68, 12)
	CCU_GATE(CLK_BUS_I2S1, "bus-i2s1", "apb1", 0x68, 13)
	CCU_GATE(CLK_BUS_I2S2, "bus-i2s2", "apb1", 0x68, 14)

	CCU_GATE(CLK_BUS_I2C0, "bus-i2c0", "apb2", 0x6c, 0)
	CCU_GATE(CLK_BUS_I2C1, "bus-i2c1", "apb2", 0x6c, 1)
	CCU_GATE(CLK_BUS_I2C2, "bus-i2c2", "apb2", 0x6c, 2)
	CCU_GATE(CLK_BUS_UART0, "bus-uart0", "apb2", 0x6c, 16)
	CCU_GATE(CLK_BUS_UART1, "bus-uart1", "apb2", 0x6c, 17)
	CCU_GATE(CLK_BUS_UART2, "bus-uart2", "apb2", 0x6c, 18)
	CCU_GATE(CLK_BUS_UART3, "bus-uart3", "apb2", 0x6c, 19)
	CCU_GATE(CLK_BUS_SCR, "bus-scr", "apb2", 0x6c, 20)

	CCU_GATE(CLK_BUS_EPHY, "bus-ephy", "ahb1", 0x70, 0)
	CCU_GATE(CLK_BUS_DBG, "bus-dbg", "ahb1", 0x70, 7)

	CCU_GATE(CLK_USBPHY0, "usb-phy0", "osc24M", 0xcc, 8)
	CCU_GATE(CLK_USBPHY1, "usb-phy1", "osc24M", 0xcc, 9)
	CCU_GATE(CLK_USBPHY2, "usb-phy2", "osc24M", 0xcc, 10)
	CCU_GATE(CLK_USBPHY3, "usb-phy3", "osc24M", 0xcc, 11)
	CCU_GATE(CLK_USBOHCI0, "usb-ohci0", "osc24M", 0xcc, 16)
	CCU_GATE(CLK_USBOHCI1, "usb-ohci1", "osc24M", 0xcc, 17)
	CCU_GATE(CLK_USBOHCI2, "usb-ohci2", "osc24M", 0xcc, 18)
	CCU_GATE(CLK_USBOHCI3, "usb-ohci3", "osc24M", 0xcc, 19)

	CCU_GATE(CLK_THS, "ths", "thsdiv", 0x74, 31)
	CCU_GATE(CLK_I2S0, "i2s0", "i2s0mux", 0xB0, 31)
	CCU_GATE(CLK_I2S1, "i2s1", "i2s1mux", 0xB4, 31)
	CCU_GATE(CLK_I2S2, "i2s2", "i2s2mux", 0xB8, 31)

	CCU_GATE(CLK_DRAM_VE, "dram-ve", "dram", 0x100, 0)
	CCU_GATE(CLK_DRAM_CSI, "dram-csi", "dram", 0x100, 1)
	CCU_GATE(CLK_DRAM_DEINTERLACE, "dram-deinterlace", "dram", 0x100, 2)
	CCU_GATE(CLK_DRAM_TS, "dram-ts", "dram", 0x100, 3)

	CCU_GATE(CLK_AC_DIG, "ac-dig", "pll_audio", 0x140, 31)

	CCU_GATE(CLK_AVS, "avs", "osc24M", 0x144, 31)

	CCU_GATE(CLK_CSI_MISC, "csi-misc", "osc24M", 0x130, 31)

	CCU_GATE(CLK_HDMI_DDC, "hdmi-ddc", "osc24M", 0x154, 31)
};

static const char *pll_cpux_parents[] = {"osc24M"};
NKMP_CLK(pll_cpux_clk,
    CLK_PLL_CPUX,				/* id */
    "pll_cpux", pll_cpux_parents,		/* name, parents */
    0x00,					/* offset */
    8, 5, 0, 0,					/* n factor */
    4, 2, 0, 0,					/* k factor */
    0, 2, 0, 0,					/* m factor */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* p factor */
    31,						/* gate */
    28, 1000,					/* lock */
    AW_CLK_HAS_GATE | AW_CLK_HAS_LOCK | AW_CLK_SCALE_CHANGE);		/* flags */

static const char *pll_audio_parents[] = {"osc24M"};
NKMP_CLK(pll_audio_clk,
    CLK_PLL_AUDIO,				/* id */
    "pll_audio", pll_audio_parents,		/* name, parents */
    0x08,					/* offset */
    8, 7, 0, 0,					/* n factor */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* k factor (fake) */
    0, 5, 0, 0,					/* m factor */
    16, 4, 0, 0,				/* p factor */
    31,						/* gate */
    28, 1000,					/* lock */
    AW_CLK_HAS_GATE | AW_CLK_HAS_LOCK);		/* flags */

static const char *pll_audio_mult_parents[] = {"pll_audio"};
FIXED_CLK(pll_audio_2x_clk,
    CLK_PLL_AUDIO_2X,			/* id */
    "pll_audio-2x",			/* name */
    pll_audio_mult_parents,		/* parent */
    0,					/* freq */
    2,					/* mult */
    1,					/* div */
    0);					/* flags */
FIXED_CLK(pll_audio_4x_clk,
    CLK_PLL_AUDIO_4X,			/* id */
    "pll_audio-4x",			/* name */
    pll_audio_mult_parents,		/* parent */
    0,					/* freq */
    4,					/* mult */
    1,					/* div */
    0);					/* flags */
FIXED_CLK(pll_audio_8x_clk,
    CLK_PLL_AUDIO_8X,			/* id */
    "pll_audio-8x",			/* name */
    pll_audio_mult_parents,		/* parent */
    0,					/* freq */
    8,					/* mult */
    1,					/* div */
    0);					/* flags */

static const char *pll_video_parents[] = {"osc24M"};
NM_CLK_WITH_FRAC(pll_video_clk,
    CLK_PLL_VIDEO,				/* id */
    "pll_video", pll_video_parents,		/* name, parents */
    0x10,					/* offset */
    8, 7, 0, 0,					/* n factor */
    0, 4, 0, 0,					/* m factor */
    31, 28, 1000,				/* gate, lock, lock retries */
    AW_CLK_HAS_LOCK,				/* flags */
    270000000, 297000000,			/* freq0, freq1 */
    24, 25);					/* mode sel, freq sel */

static const char *pll_ve_parents[] = {"osc24M"};
NM_CLK_WITH_FRAC(pll_ve_clk,
    CLK_PLL_VE,				/* id */
    "pll_ve", pll_ve_parents,			/* name, parents */
    0x18,					/* offset */
    8, 7, 0, 0,					/* n factor */
    0, 4, 0, 0,					/* m factor */
    31, 28, 1000,				/* gate, lock, lock retries */
    AW_CLK_HAS_LOCK,				/* flags */
    270000000, 297000000,			/* freq0, freq1 */
    24, 25);					/* mode sel, freq sel */

static const char *pll_ddr_parents[] = {"osc24M"};
NKMP_CLK_WITH_UPDATE(pll_ddr_clk,
    CLK_PLL_DDR,				/* id */
    "pll_ddr", pll_ddr_parents,			/* name, parents */
    0x20,					/* offset */
    8, 5, 0, 0,					/* n factor */
    4, 2, 0, 0,					/* k factor */
    0, 2, 0, 0,					/* m factor */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* p factor (fake) */
    31,						/* gate */
    28, 1000,					/* lock */
    20,						/* update */
    AW_CLK_HAS_GATE | AW_CLK_HAS_LOCK);		/* flags */

static const char *pll_periph0_parents[] = {"osc24M"};
static const char *pll_periph0_2x_parents[] = {"pll_periph0"};
NKMP_CLK(pll_periph0_clk,
    CLK_PLL_PERIPH0,				/* id */
    "pll_periph0", pll_periph0_parents,		/* name, parents */
    0x28,					/* offset */
    8, 5, 0, 0,					/* n factor */
    4, 2, 0, 0,					/* k factor */
    0, 0, 2, AW_CLK_FACTOR_FIXED,		/* m factor (fake) */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* p factor (fake) */
    31,						/* gate */
    28, 1000,					/* lock */
    AW_CLK_HAS_GATE | AW_CLK_HAS_LOCK);		/* flags */
FIXED_CLK(pll_periph0_2x_clk,
    CLK_PLL_PERIPH0_2X,			/* id */
    "pll_periph0-2x",			/* name */
    pll_periph0_2x_parents,		/* parent */
    0,					/* freq */
    2,					/* mult */
    1,					/* div */
    0);					/* flags */

static const char *pll_gpu_parents[] = {"osc24M"};
NM_CLK_WITH_FRAC(pll_gpu_clk,
    CLK_PLL_GPU,				/* id */
    "pll_gpu", pll_gpu_parents,			/* name, parents */
    0x38,					/* offset */
    8, 7, 0, 0,					/* n factor */
    0, 4, 0, 0,					/* m factor */
    31, 28, 1000,				/* gate, lock, lock retries */
    AW_CLK_HAS_LOCK,				/* flags */
    270000000, 297000000,			/* freq0, freq1 */
    24, 25);					/* mode sel, freq sel */

static const char *pll_periph1_parents[] = {"osc24M"};
NKMP_CLK(pll_periph1_clk,
    CLK_PLL_PERIPH1,				/* id */
    "pll_periph1", pll_periph1_parents,		/* name, parents */
    0x44,					/* offset */
    8, 5, 0, 0,					/* n factor */
    4, 2, 0, 0,					/* k factor */
    0, 0, 2, AW_CLK_FACTOR_FIXED,		/* m factor (fake) */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* p factor (fake) */
    31,						/* gate */
    28, 1000,					/* lock */
    AW_CLK_HAS_GATE | AW_CLK_HAS_LOCK);		/* flags */

static const char *pll_de_parents[] = {"osc24M"};
NM_CLK_WITH_FRAC(pll_de_clk,
    CLK_PLL_DE,					/* id */
    "pll_de", pll_de_parents,			/* name, parents */
    0x48,					/* offset */
    8, 7, 0, 0,					/* n factor */
    0, 4, 0, 0,					/* m factor */
    31, 28, 1000,				/* gate, lock, lock retries */
    AW_CLK_HAS_LOCK,				/* flags */
    270000000, 297000000,			/* freq0, freq1 */
    24, 25);					/* mode sel, freq sel */

static const char *cpux_parents[] = {"osc32k", "osc24M", "pll_cpux", "pll_cpux"};
MUX_CLK(cpux_clk,
    CLK_CPUX,			/* id */
    "cpux", cpux_parents,	/* name, parents */
    0x50, 16, 2);		/* offset, shift, width */

static const char *axi_parents[] = {"cpux"};
DIV_CLK(axi_clk,
    CLK_AXI,			/* id */
    "axi", axi_parents,		/* name, parents */
    0x50,			/* offset */
    0, 2,			/* shift, width */
    0, NULL);			/* flags, div table */

static const char *ahb1_parents[] = {"osc32k", "osc24M", "axi", "pll_periph0"};
PREDIV_CLK(ahb1_clk, CLK_AHB1,					/* id */
    "ahb1", ahb1_parents,					/* name, parents */
    0x54,							/* offset */
    12, 2,							/* mux */
    4, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,			/* div */
    6, 2, 0, AW_CLK_FACTOR_HAS_COND,				/* prediv */
    12, 2, 3);							/* prediv condition */

static const char *apb1_parents[] = {"ahb1"};
static struct clk_div_table apb1_div_table[] = {
	{ .value = 0, .divider = 2, },
	{ .value = 1, .divider = 2, },
	{ .value = 2, .divider = 4, },
	{ .value = 3, .divider = 8, },
	{ },
};
DIV_CLK(apb1_clk,
    CLK_APB1,			/* id */
    "apb1", apb1_parents,	/* name, parents */
    0x54,			/* offset */
    8, 2,			/* shift, width */
    CLK_DIV_WITH_TABLE,		/* flags */
    apb1_div_table);		/* div table */

static const char *apb2_parents[] = {"osc32k", "osc24M", "pll_periph0", "pll_periph0"};
NM_CLK(apb2_clk,
    CLK_APB2,					/* id */
    "apb2", apb2_parents,			/* name, parents */
    0x58,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 5, 0, 0,					/* m factor */
    24, 2,					/* mux */
    0,						/* gate */
    AW_CLK_HAS_MUX);

static const char *ahb2_parents[] = {"ahb1", "pll_periph0"};
PREDIV_CLK(ahb2_clk, CLK_AHB2,					/* id */
    "ahb2", ahb2_parents,					/* name, parents */
    0x5c,							/* offset */
    0, 2,							/* mux */
    0, 0, 1, AW_CLK_FACTOR_FIXED,				/* div */
    0, 0, 2, AW_CLK_FACTOR_HAS_COND | AW_CLK_FACTOR_FIXED,	/* prediv */
    0, 2, 1);							/* prediv condition */

static const char *ths_parents[] = {"osc24M"};
static struct clk_div_table ths_div_table[] = {
	{ .value = 0, .divider = 1, },
	{ .value = 1, .divider = 2, },
	{ .value = 2, .divider = 4, },
	{ .value = 3, .divider = 6, },
	{ },
};
DIV_CLK(thsdiv_clk,
    0,				/* id */
    "thsdiv", ths_parents,	/* name, parents */
    0x74,			/* offset */
    0, 2,			/* shift, width */
    CLK_DIV_WITH_TABLE,		/* flags */
    ths_div_table);		/* div table */

static const char *mod_parents[] = {"osc24M", "pll_periph0", "pll_periph1"};
NM_CLK(nand_clk,
    CLK_NAND, "nand", mod_parents,		/* id, name, parents */
    0x80,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE | AW_CLK_HAS_MUX);		/* flags */

NM_CLK(mmc0_clk,
    CLK_MMC0, "mmc0", mod_parents,		/* id, name, parents */
    0x88,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE | AW_CLK_HAS_MUX |
    AW_CLK_REPARENT);				/* flags */

NM_CLK(mmc1_clk,
    CLK_MMC1, "mmc1", mod_parents,		/* id, name, parents */
    0x8c,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE | AW_CLK_HAS_MUX |
    AW_CLK_REPARENT);				/* flags */

NM_CLK(mmc2_clk,
    CLK_MMC2, "mmc2", mod_parents,		/* id, name, parents */
    0x90,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE | AW_CLK_HAS_MUX |
    AW_CLK_REPARENT);				/* flags */

static const char *ts_parents[] = {"osc24M", "pll_periph0"};
NM_CLK(ts_clk,
    CLK_TS, "ts", ts_parents,			/* id, name, parents */
    0x98,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE | AW_CLK_HAS_MUX);		/* flags */

NM_CLK(ce_clk,
    CLK_CE, "ce", mod_parents,			/* id, name, parents */
    0x9C,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE | AW_CLK_HAS_MUX);		/* flags */

NM_CLK(spi0_clk,
    CLK_SPI0, "spi0", mod_parents,		/* id, name, parents */
    0xA0,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE | AW_CLK_HAS_MUX |
    AW_CLK_REPARENT);				/* flags */

NM_CLK(spi1_clk,
    CLK_SPI1, "spi1", mod_parents,		/* id, name, parents */
    0xA4,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE | AW_CLK_HAS_MUX |
    AW_CLK_REPARENT);				/* flags */

static const char *i2s_parents[] = {"pll_audio-8x", "pll_audio-4x", "pll_audio-2x", "pll_audio"};
MUX_CLK(i2s0mux_clk,
    0, "i2s0mux", i2s_parents,			/* id, name, parents */
    0xb0, 16, 2);				/* offset, mux shift, mux width */
MUX_CLK(i2s1mux_clk,
    0, "i2s1mux", i2s_parents,			/* id, name, parents */
    0xb4, 16, 2);				/* offset, mux shift, mux width */
MUX_CLK(i2s2mux_clk,
    0, "i2s2mux", i2s_parents,			/* id, name, parents */
    0xb8, 16, 2);				/* offset, mux shift, mux width */

static const char *spdif_parents[] = {"pll_audio"};
NM_CLK(spdif_clk,
    CLK_SPDIF, "spdif", spdif_parents,		/* id, name, parents */
    0xC0,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake); */
    0, 4, 0, 0,					/* m factor */
    0, 0,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE);				/* flags */

static const char *dram_parents[] = {"pll_ddr", "pll_periph0-2x"};
NM_CLK(dram_clk,
    CLK_DRAM, "dram", dram_parents,		/* id, name, parents */
    0xF4,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 4, 0, 0,					/* m factor */
    20, 2,					/* mux */
    0,						/* gate */
    AW_CLK_HAS_MUX);				/* flags */

static const char *de_parents[] = {"pll_periph0-2x", "pll_de"};
NM_CLK(de_clk,
    CLK_DE, "de", de_parents,			/* id, name, parents */
    0x104,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

static const char *tcon0_parents[] = {"pll_video"};
NM_CLK(tcon0_clk,
    CLK_TCON0, "tcon0", tcon0_parents,		/* id, name, parents */
    0x118,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

static const char *tve_parents[] = {"pll_de", "pll_periph1"};
NM_CLK(tve_clk,
    CLK_TVE, "tve", tve_parents,		/* id, name, parents */
    0x120,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

static const char *deinterlace_parents[] = {"pll_periph0", "pll_periph1"};
NM_CLK(deinterlace_clk,
    CLK_DEINTERLACE, "deinterlace", deinterlace_parents,	/* id, name, parents */
    0x124,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

static const char *csi_sclk_parents[] = {"pll_periph0", "pll_periph1"};
NM_CLK(csi_sclk_clk,
    CLK_CSI_SCLK, "csi-sclk", csi_sclk_parents,	/* id, name, parents */
    0x134,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    16, 4, 0, 0,				/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

static const char *csi_mclk_parents[] = {"osc24M", "pll_video", "pll_periph1"};
NM_CLK(csi_mclk_clk,
    CLK_CSI_MCLK, "csi-mclk", csi_mclk_parents,	/* id, name, parents */
    0x134,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 4, 0, 0,					/* m factor */
    8, 2,					/* mux */
    15,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

static const char *ve_parents[] = {"pll_ve"};
NM_CLK(ve_clk,
    CLK_VE, "ve", ve_parents,			/* id, name, parents */
    0x13C,					/* offset */
    16, 3, 0, 0,				/* n factor */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* m factor (fake) */
    0, 0,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE);				/* flags */

static const char *hdmi_parents[] = {"pll_video"};
NM_CLK(hdmi_clk,
    CLK_HDMI, "hdmi", hdmi_parents,		/* id, name, parents */
    0x150,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

static const char *mbus_parents[] = {"osc24M", "pll_periph0-2x", "pll_ddr"};
NM_CLK(mbus_clk,
    CLK_MBUS, "mbus", mbus_parents,		/* id, name, parents */
    0x15C,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 3, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

static const char *gpu_parents[] = {"pll_gpu"};
NM_CLK(gpu_clk,
    CLK_GPU, "gpu", gpu_parents,		/* id, name, parents */
    0x1A0,					/* offset */
    0, 2, 0, 0,					/* n factor */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* m factor (fake) */
    0, 0,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE);				/* flags */

static struct aw_ccung_clk h3_ccu_clks[] = {
	{ .type = AW_CLK_NKMP, .clk.nkmp = &pll_cpux_clk},
	{ .type = AW_CLK_NKMP, .clk.nkmp = &pll_audio_clk},
	{ .type = AW_CLK_NKMP, .clk.nkmp = &pll_periph0_clk},
	{ .type = AW_CLK_NKMP, .clk.nkmp = &pll_periph1_clk},
	{ .type = AW_CLK_NKMP, .clk.nkmp = &pll_ddr_clk},
	{ .type = AW_CLK_NM, .clk.nm = &pll_video_clk},
	{ .type = AW_CLK_NM, .clk.nm = &pll_ve_clk},
	{ .type = AW_CLK_NM, .clk.nm = &pll_gpu_clk},
	{ .type = AW_CLK_NM, .clk.nm = &pll_de_clk},
	{ .type = AW_CLK_NM, .clk.nm = &apb2_clk},
	{ .type = AW_CLK_NM, .clk.nm = &nand_clk},
	{ .type = AW_CLK_NM, .clk.nm = &mmc0_clk},
	{ .type = AW_CLK_NM, .clk.nm = &mmc1_clk},
	{ .type = AW_CLK_NM, .clk.nm = &mmc2_clk},
	{ .type = AW_CLK_NM, .clk.nm = &ts_clk},
	{ .type = AW_CLK_NM, .clk.nm = &ce_clk},
	{ .type = AW_CLK_NM, .clk.nm = &spi0_clk},
	{ .type = AW_CLK_NM, .clk.nm = &spi1_clk},
	{ .type = AW_CLK_NM, .clk.nm = &spdif_clk},
	{ .type = AW_CLK_NM, .clk.nm = &dram_clk},
	{ .type = AW_CLK_NM, .clk.nm = &de_clk},
	{ .type = AW_CLK_NM, .clk.nm = &tcon0_clk},
	{ .type = AW_CLK_NM, .clk.nm = &tve_clk},
	{ .type = AW_CLK_NM, .clk.nm = &deinterlace_clk},
	{ .type = AW_CLK_NM, .clk.nm = &csi_sclk_clk},
	{ .type = AW_CLK_NM, .clk.nm = &csi_mclk_clk},
	{ .type = AW_CLK_NM, .clk.nm = &ve_clk},
	{ .type = AW_CLK_NM, .clk.nm = &hdmi_clk},
	{ .type = AW_CLK_NM, .clk.nm = &mbus_clk},
	{ .type = AW_CLK_NM, .clk.nm = &gpu_clk},
	{ .type = AW_CLK_PREDIV_MUX, .clk.prediv_mux = &ahb1_clk},
	{ .type = AW_CLK_PREDIV_MUX, .clk.prediv_mux = &ahb2_clk},
	{ .type = AW_CLK_MUX, .clk.mux = &cpux_clk},
	{ .type = AW_CLK_MUX, .clk.mux = &i2s0mux_clk},
	{ .type = AW_CLK_MUX, .clk.mux = &i2s1mux_clk},
	{ .type = AW_CLK_MUX, .clk.mux = &i2s2mux_clk},
	{ .type = AW_CLK_DIV, .clk.div = &axi_clk},
	{ .type = AW_CLK_DIV, .clk.div = &apb1_clk},
	{ .type = AW_CLK_DIV, .clk.div = &thsdiv_clk},
	{ .type = AW_CLK_FIXED, .clk.fixed = &pll_periph0_2x_clk},
	{ .type = AW_CLK_FIXED, .clk.fixed = &pll_audio_2x_clk},
	{ .type = AW_CLK_FIXED, .clk.fixed = &pll_audio_4x_clk},
	{ .type = AW_CLK_FIXED, .clk.fixed = &pll_audio_8x_clk},
};

static struct aw_clk_init h3_init_clks[] = {
	{"ahb1", "pll_periph0", 0, false},
	{"ahb2", "pll_periph0", 0, false},
	{"dram", "pll_ddr", 0, false},
};

static struct ofw_compat_data compat_data[] = {
#if defined(SOC_ALLWINNER_H3)
	{ "allwinner,sun8i-h3-ccu", 1 },
#endif
#if defined(SOC_ALLWINNER_H5)
	{ "allwinner,sun50i-h5-ccu", 1 },
#endif
	{ NULL, 0},
};

static int
ccu_h3_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Allwinner H3/H5 Clock Control Unit NG");
	return (BUS_PROBE_DEFAULT);
}

static int
ccu_h3_attach(device_t dev)
{
	struct aw_ccung_softc *sc;

	sc = device_get_softc(dev);

	sc->resets = h3_ccu_resets;
	sc->nresets = nitems(h3_ccu_resets);
	sc->gates = h3_ccu_gates;
	sc->ngates = nitems(h3_ccu_gates);
	sc->clks = h3_ccu_clks;
	sc->nclks = nitems(h3_ccu_clks);
	sc->clk_init = h3_init_clks;
	sc->n_clk_init = nitems(h3_init_clks);

	return (aw_ccung_attach(dev));
}

static device_method_t ccu_h3ng_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ccu_h3_probe),
	DEVMETHOD(device_attach,	ccu_h3_attach),

	DEVMETHOD_END
};

static devclass_t ccu_h3ng_devclass;

DEFINE_CLASS_1(ccu_h3ng, ccu_h3ng_driver, ccu_h3ng_methods,
  sizeof(struct aw_ccung_softc), aw_ccung_driver);

EARLY_DRIVER_MODULE(ccu_h3ng, simplebus, ccu_h3ng_driver,
    ccu_h3ng_devclass, 0, 0, BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);
