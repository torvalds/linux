/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017 Kyle Evans <kevans@FreeBSD.org>
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

#include <dev/extres/clk/clk_div.h>
#include <dev/extres/clk/clk_fixed.h>
#include <dev/extres/clk/clk_mux.h>

#include <arm/allwinner/clkng/aw_ccung.h>

#include <gnu/dts/include/dt-bindings/clock/sun8i-a83t-ccu.h>
#include <gnu/dts/include/dt-bindings/reset/sun8i-a83t-ccu.h>

/* Non-exported clocks */

#define	CLK_PLL_C0CPUX		0
#define	CLK_PLL_C1CPUX		1
#define	CLK_PLL_AUDIO		2
#define	CLK_PLL_VIDEO0		3
#define	CLK_PLL_VE		4
#define	CLK_PLL_DDR		5

#define	CLK_PLL_GPU		7
#define	CLK_PLL_HSIC		8
#define	CLK_PLL_VIDEO1		10

#define	CLK_AXI0		13
#define	CLK_AXI1		14
#define	CLK_AHB1		15
#define	CLK_APB1		16
#define	CLK_APB2		17
#define	CLK_AHB2		18

#define	CLK_CCI400		58

#define CLK_DRAM		82

#define	CLK_MBUS		95

/* Non-exported fixed clocks */
#define CLK_OSC_12M		150


static struct aw_ccung_reset a83t_ccu_resets[] = {
	CCU_RESET(RST_USB_PHY0, 0xcc, 0)
	CCU_RESET(RST_USB_PHY1, 0xcc, 1)
	CCU_RESET(RST_USB_HSIC, 0xcc, 2)

	CCU_RESET(RST_DRAM, 0xf4, 31)
	CCU_RESET(RST_MBUS, 0xfc, 31)

	CCU_RESET(RST_BUS_MIPI_DSI, 0x2c0, 1)
	CCU_RESET(RST_BUS_SS, 0x2c0, 5)
	CCU_RESET(RST_BUS_DMA, 0x2c0, 6)
	CCU_RESET(RST_BUS_MMC0, 0x2c0, 8)
	CCU_RESET(RST_BUS_MMC1, 0x2c0, 9)
	CCU_RESET(RST_BUS_MMC2, 0x2c0, 10)
	CCU_RESET(RST_BUS_NAND, 0x2c0, 13)
	CCU_RESET(RST_BUS_DRAM, 0x2c0, 14)
	CCU_RESET(RST_BUS_EMAC, 0x2c0, 17)
	CCU_RESET(RST_BUS_HSTIMER, 0x2c0, 19)
	CCU_RESET(RST_BUS_SPI0, 0x2c0, 20)
	CCU_RESET(RST_BUS_SPI1, 0x2c0, 21)
	CCU_RESET(RST_BUS_OTG, 0x2c0, 24)
	CCU_RESET(RST_BUS_EHCI0, 0x2c0, 26)
	CCU_RESET(RST_BUS_EHCI1, 0x2c0, 27)
	CCU_RESET(RST_BUS_OHCI0, 0x2c0, 29)

	CCU_RESET(RST_BUS_VE, 0x2c4, 0)
	CCU_RESET(RST_BUS_TCON0, 0x2c4, 4)
	CCU_RESET(RST_BUS_TCON1, 0x2c4, 5)
	CCU_RESET(RST_BUS_CSI, 0x2c4, 8)
	CCU_RESET(RST_BUS_HDMI0, 0x2c4, 10)
	CCU_RESET(RST_BUS_HDMI1, 0x2c4, 11)
	CCU_RESET(RST_BUS_DE, 0x2c4, 12)
	CCU_RESET(RST_BUS_GPU, 0x2c4, 20)
	CCU_RESET(RST_BUS_MSGBOX, 0x2c4, 21)
	CCU_RESET(RST_BUS_SPINLOCK, 0x2c4, 22)

	CCU_RESET(RST_BUS_LVDS, 0x2c8, 0)

	CCU_RESET(RST_BUS_SPDIF, 0x2d0, 1)
	CCU_RESET(RST_BUS_I2S0, 0x2d0, 12)
	CCU_RESET(RST_BUS_I2S1, 0x2d0, 13)
	CCU_RESET(RST_BUS_I2S2, 0x2d0, 14)
	CCU_RESET(RST_BUS_TDM, 0x2d0, 15)

	CCU_RESET(RST_BUS_I2C0, 0x2d8, 0)
	CCU_RESET(RST_BUS_I2C1, 0x2d8, 1)
	CCU_RESET(RST_BUS_I2C2, 0x2d8, 2)
	CCU_RESET(RST_BUS_UART0, 0x2d8, 16)
	CCU_RESET(RST_BUS_UART1, 0x2d8, 17)
	CCU_RESET(RST_BUS_UART2, 0x2d8, 18)
	CCU_RESET(RST_BUS_UART3, 0x2d8, 19)
	CCU_RESET(RST_BUS_UART4, 0x2d8, 20)
};

static struct aw_ccung_gate a83t_ccu_gates[] = {
	CCU_GATE(CLK_BUS_MIPI_DSI, "bus-mipi-dsi", "ahb1", 0x60, 1)
	CCU_GATE(CLK_BUS_SS, "bus-ss", "ahb1", 0x60, 5)
	CCU_GATE(CLK_BUS_DMA, "bus-dma", "ahb1", 0x60, 6)
	CCU_GATE(CLK_BUS_MMC0, "bus-mmc0", "ahb1", 0x60, 8)
	CCU_GATE(CLK_BUS_MMC1, "bus-mmc1", "ahb1", 0x60, 9)
	CCU_GATE(CLK_BUS_MMC2, "bus-mmc2", "ahb1", 0x60, 10)
	CCU_GATE(CLK_BUS_NAND, "bus-nand", "ahb1", 0x60, 13)
	CCU_GATE(CLK_BUS_DRAM, "bus-dram", "ahb1", 0x60, 14)
	CCU_GATE(CLK_BUS_EMAC, "bus-emac", "ahb1", 0x60, 17)
	CCU_GATE(CLK_BUS_HSTIMER, "bus-hstimer", "ahb1", 0x60, 19)
	CCU_GATE(CLK_BUS_SPI0, "bus-spi0", "ahb1", 0x60, 20)
	CCU_GATE(CLK_BUS_SPI1, "bus-spi1", "ahb1", 0x60, 21)
	CCU_GATE(CLK_BUS_OTG, "bus-otg", "ahb1", 0x60, 24)
	CCU_GATE(CLK_BUS_EHCI0, "bus-ehci0", "ahb2", 0x60, 26)
	CCU_GATE(CLK_BUS_EHCI1, "bus-ehci1", "ahb2", 0x60, 27)
	CCU_GATE(CLK_BUS_OHCI0, "bus-ohci0", "ahb2", 0x60, 29)

	CCU_GATE(CLK_BUS_VE, "bus-ve", "ahb1", 0x64, 0)
	CCU_GATE(CLK_BUS_TCON0, "bus-tcon0", "ahb1", 0x64, 4)
	CCU_GATE(CLK_BUS_TCON1, "bus-tcon1", "ahb1", 0x64, 5)
	CCU_GATE(CLK_BUS_CSI, "bus-csi", "ahb1", 0x64, 8)
	CCU_GATE(CLK_BUS_HDMI, "bus-hdmi", "ahb1", 0x64, 11)
	CCU_GATE(CLK_BUS_DE, "bus-de", "ahb1", 0x64, 12)
	CCU_GATE(CLK_BUS_GPU, "bus-gpu", "ahb1", 0x64, 20)
	CCU_GATE(CLK_BUS_MSGBOX, "bus-msgbox", "ahb1", 0x64, 21)
	CCU_GATE(CLK_BUS_SPINLOCK, "bus-spinlock", "ahb1", 0x64, 22)

	CCU_GATE(CLK_BUS_SPDIF, "bus-spdif", "apb1", 0x68, 1)
	CCU_GATE(CLK_BUS_PIO, "bus-pio", "apb1", 0x68, 5)
	CCU_GATE(CLK_BUS_I2S0, "bus-i2s0", "apb1", 0x68, 12)
	CCU_GATE(CLK_BUS_I2S1, "bus-i2s1", "apb1", 0x68, 13)
	CCU_GATE(CLK_BUS_I2S2, "bus-i2s2", "apb1", 0x68, 14)
	CCU_GATE(CLK_BUS_TDM, "bus-tdm", "apb1", 0x68, 15)

	CCU_GATE(CLK_BUS_I2C0, "bus-i2c0", "apb2", 0x6c, 0)
	CCU_GATE(CLK_BUS_I2C1, "bus-i2c1", "apb2", 0x6c, 1)
	CCU_GATE(CLK_BUS_I2C2, "bus-i2c2", "apb2", 0x6c, 2)
	CCU_GATE(CLK_BUS_UART0, "bus-uart0", "apb2", 0x6c, 16)
	CCU_GATE(CLK_BUS_UART1, "bus-uart1", "apb2", 0x6c, 17)
	CCU_GATE(CLK_BUS_UART2, "bus-uart2", "apb2", 0x6c, 18)
	CCU_GATE(CLK_BUS_UART3, "bus-uart3", "apb2", 0x6c, 19)
	CCU_GATE(CLK_BUS_UART4, "bus-uart4", "apb2", 0x6c, 20)

	CCU_GATE(CLK_USB_PHY0, "usb-phy0", "osc24M", 0xcc, 8)
	CCU_GATE(CLK_USB_PHY1, "usb-phy1", "osc24M", 0xcc, 9)
	CCU_GATE(CLK_USB_HSIC, "usb-hsic", "pll_hsic", 0xcc, 10)
	CCU_GATE(CLK_USB_HSIC_12M, "usb-hsic-12M", "osc12M", 0xcc, 11)
	CCU_GATE(CLK_USB_OHCI0, "usb-ohci0", "osc12M", 0xcc, 16)

	CCU_GATE(CLK_DRAM_VE, "dram-ve", "dram", 0x100, 0)
	CCU_GATE(CLK_DRAM_CSI, "dram-csi", "dram", 0x100, 1)

	CCU_GATE(CLK_CSI_MISC, "csi-misc", "osc24M", 0x130, 16)
	CCU_GATE(CLK_MIPI_CSI, "mipi-csi", "osc24M", 0x130, 31)

	CCU_GATE(CLK_AVS, "avs", "osc24M", 0x144, 31)

	CCU_GATE(CLK_HDMI_SLOW, "hdmi-ddc", "osc24M", 0x154, 31)
};

static const char *osc12m_parents[] = {"osc24M"};
FIXED_CLK(osc12m_clk,
    CLK_OSC_12M,				/* id */
    "osc12M", osc12m_parents,			/* name, parents */
    0,						/* freq */
    1,						/* mult */
    2,						/* div */
    0);						/* flags */

/* CPU PLL are 24Mhz * N / P */
static const char *pll_c0cpux_parents[] = {"osc24M"};
static const char *pll_c1cpux_parents[] = {"osc24M"};
NKMP_CLK(pll_c0cpux_clk,
    CLK_PLL_C0CPUX,				/* id */
    "pll_c0cpux", pll_c0cpux_parents,		/* name, parents */
    0x00,					/* offset */
    8, 8, 0, AW_CLK_FACTOR_ZERO_BASED,		/* n factor */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* k factor (fake) */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* m factor */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* p factor (fake) */
    0, 0,					/* lock */
    31,						/* gate */
    AW_CLK_HAS_GATE | AW_CLK_SCALE_CHANGE);	/* flags */
NKMP_CLK(pll_c1cpux_clk,
    CLK_PLL_C1CPUX,				/* id */
    "pll_c1cpux", pll_c1cpux_parents,		/* name, parents */
    0x04,					/* offset */
    8, 8, 0, AW_CLK_FACTOR_ZERO_BASED,		/* n factor */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* k factor (fake) */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* m factor */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* p factor (fake) */
    0, 0,					/* lock */
    31,						/* gate */
    AW_CLK_HAS_GATE | AW_CLK_SCALE_CHANGE);	/* flags */

static const char *pll_audio_parents[] = {"osc24M"};
NKMP_CLK(pll_audio_clk,
    CLK_PLL_AUDIO,				/* id */
    "pll_audio", pll_audio_parents,		/* name, parents */
    0x08,					/* offset */
    8, 8, 0, AW_CLK_FACTOR_ZERO_BASED,		/* n factor */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* k factor (fake) */
    16, 1, 0, 0,				/* m factor */
    18, 1, 0, 0,				/* p factor */
    31,						/* gate */
    0, 0,					/* lock */
    AW_CLK_HAS_GATE);				/* flags */

static const char *pll_video0_parents[] = {"osc24M"};
NKMP_CLK(pll_video0_clk,
    CLK_PLL_VIDEO0,				/* id */
    "pll_video0", pll_video0_parents,		/* name, parents */
    0x10,					/* offset */
    8, 8, 0, AW_CLK_FACTOR_ZERO_BASED,		/* n factor */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* k factor (fake) */
    16, 1, 0, 0,				/* m factor */
    0, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* p factor */
    31,						/* gate */
    0, 0,					/* lock */
    AW_CLK_HAS_GATE);				/* flags */

static const char *pll_ve_parents[] = {"osc24M"};
NKMP_CLK(pll_ve_clk,
    CLK_PLL_VE,					/* id */
    "pll_ve", pll_ve_parents,			/* name, parents */
    0x18,					/* offset */
    8, 8, 0, AW_CLK_FACTOR_ZERO_BASED,		/* n factor */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* k factor (fake) */
    16, 1, 0, 0,				/* m factor */
    18, 1, 0, 0,				/* p factor */
    31,						/* gate */
    0, 0,					/* lock */
    AW_CLK_HAS_GATE);				/* flags */

static const char *pll_ddr_parents[] = {"osc24M"};
NKMP_CLK(pll_ddr_clk,
    CLK_PLL_DDR,				/* id */
    "pll_ddr", pll_ddr_parents,			/* name, parents */
    0x20,					/* offset */
    8, 5, 0, 0,					/* n factor */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* k factor (fake) */
    16, 1, 0, 0,				/* m factor */
    18, 1, 0, 0,				/* p factor */
    31,						/* gate */
    0, 0,					/* lock */
    AW_CLK_HAS_GATE);				/* flags */

static const char *pll_periph_parents[] = {"osc24M"};
NKMP_CLK(pll_periph_clk,
    CLK_PLL_PERIPH,				/* id */
    "pll_periph", pll_periph_parents,		/* name, parents */
    0x28,					/* offset */
    8, 8, 0, AW_CLK_FACTOR_ZERO_BASED,		/* n factor */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* k factor (fake) */
    16, 1, 1, 0,				/* m factor */
    18, 1, 1, 0,				/* p factor */
    31,						/* gate */
    0, 0,					/* lock */
    AW_CLK_HAS_GATE);				/* flags */

static const char *pll_gpu_parents[] = {"osc24M"};
NKMP_CLK(pll_gpu_clk,
    CLK_PLL_GPU,				/* id */
    "pll_gpu", pll_gpu_parents,			/* name, parents */
    0x38,					/* offset */
    8, 8, 0, AW_CLK_FACTOR_ZERO_BASED,		/* n factor */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* k factor (fake) */
    16, 1, 1, 0,				/* m factor */
    18, 1, 1, 0,				/* p factor */
    31,						/* gate */
    0, 0,					/* lock */
    AW_CLK_HAS_GATE);				/* flags */

static const char *pll_hsic_parents[] = {"osc24M"};
NKMP_CLK(pll_hsic_clk,
    CLK_PLL_HSIC,				/* id */
    "pll_hsic", pll_hsic_parents,		/* name, parents */
    0x44,					/* offset */
    8, 8, 0, AW_CLK_FACTOR_ZERO_BASED,		/* n factor */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* k factor (fake) */
    16, 1, 1, 0,				/* m factor */
    18, 1, 1, 0,				/* p factor */
    31,						/* gate */
    0, 0,					/* lock */
    AW_CLK_HAS_GATE);				/* flags */

static const char *pll_de_parents[] = {"osc24M"};
NKMP_CLK(pll_de_clk,
    CLK_PLL_DE,					/* id */
    "pll_de", pll_de_parents,			/* name, parents */
    0x48,					/* offset */
    8, 8, 0, AW_CLK_FACTOR_ZERO_BASED,		/* n factor */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* k factor (fake) */
    16, 1, 1, 0,				/* m factor */
    18, 1, 1, 0,				/* p factor */
    31,						/* gate */
    0, 0,					/* lock */
    AW_CLK_HAS_GATE);				/* flags */

static const char *pll_video1_parents[] = {"osc24M"};
NKMP_CLK(pll_video1_clk,
    CLK_PLL_VIDEO1,				/* id */
    "pll_video1", pll_video1_parents,		/* name, parents */
    0x4c,					/* offset */
    8, 8, 0, AW_CLK_FACTOR_ZERO_BASED,		/* n factor */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* k factor (fake) */
    16, 1, 1, 0,				/* m factor */
    0, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* p factor */
    31,						/* gate */
    0, 0,					/* lock */
    AW_CLK_HAS_GATE);				/* flags */

static const char *c0cpux_parents[] = {"osc24M", "pll_c0cpux"};
MUX_CLK(c0cpux_clk,
    CLK_C0CPUX,					/* id */
    "c0cpux", c0cpux_parents,			/* name, parents */
    0x50, 12, 1);				/* offset, shift, width */

static const char *c1cpux_parents[] = {"osc24M", "pll_c1cpux"};
MUX_CLK(c1cpux_clk,
    CLK_C1CPUX,					/* id */
    "c1cpux", c1cpux_parents,			/* name, parents */
    0x50, 28, 1);				/* offset, shift, width */

static const char *axi0_parents[] = {"c0cpux"};
DIV_CLK(axi0_clk,
    CLK_AXI0,					/* id */
    "axi0", axi0_parents,			/* name, parents */
    0x50,					/* offset */
    0, 2,					/* shift, width */
    0, NULL);					/* flags, div table */

static const char *axi1_parents[] = {"c1cpux"};
DIV_CLK(axi1_clk,
    CLK_AXI1,					/* id */
    "axi1", axi1_parents,			/* name, parents */
    0x50,					/* offset */
    16, 2,					/* shift, width */
    0, NULL);					/* flags, div table */

static const char *ahb1_parents[] = {"osc16M-d512", "osc24M", "pll_periph", "pll_periph"};
PREDIV_CLK_WITH_MASK(ahb1_clk,
    CLK_AHB1,					/* id */
    "ahb1", ahb1_parents,			/* name, parents */
    0x54,					/* offset */
    12, 2,					/* mux */
    4, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* div */
    6, 2, 0, AW_CLK_FACTOR_HAS_COND,		/* prediv */
    (2 << 12), (2 << 12));			/* prediv condition */

static const char *apb1_parents[] = {"ahb1"};
DIV_CLK(apb1_clk,
    CLK_APB1,					/* id */
    "apb1", apb1_parents,			/* name, parents */
    0x54,					/* offset */
    8, 2,					/* shift, width */
    0, NULL);					/* flags, div table */

static const char *apb2_parents[] = {"osc16M-d512", "osc24M", "pll_periph", "pll_periph"};
NM_CLK(apb2_clk,
    CLK_APB2,					/* id */
    "apb2", apb2_parents,			/* name, parents */
    0x58,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 5, 0, 0,					/* m factor */
    24, 2,					/* mux */
    0,						/* gate */
    AW_CLK_HAS_MUX);

static const char *ahb2_parents[] = {"ahb1", "pll_periph"};
PREDIV_CLK(ahb2_clk,
    CLK_AHB2,							/* id */
    "ahb2", ahb2_parents,					/* name, parents */
    0x5c,
    0, 2,							/* mux */
    0, 0, 1, AW_CLK_FACTOR_FIXED,				/* div (fake) */
    0, 0, 2, AW_CLK_FACTOR_HAS_COND | AW_CLK_FACTOR_FIXED,	/* prediv */
    0, 2, 1);							/* prediv cond */

/* Actually has a divider, but we don't use it */
static const char *cci400_parents[] = {"osc24M", "pll_periph", "pll_hsic"};
MUX_CLK(cci400_clk,
    CLK_CCI400,					/* id */
    "cci400", cci400_parents,			/* name, parents */
    0x78, 24, 2);				/* offset, shift, width */

static const char *mod_parents[] = {"osc24M", "pll_periph"};

NM_CLK(nand_clk,
    CLK_NAND,					/* id */
    "nand", mod_parents,			/* name, parents */
    0x80,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE | AW_CLK_HAS_MUX);

NM_CLK(mmc0_clk,
    CLK_MMC0,					/* id */
    "mmc0", mod_parents,			/* name, parents */
    0x88,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE | AW_CLK_HAS_MUX |
    AW_CLK_REPARENT);
NM_CLK(mmc1_clk,
    CLK_MMC1,					/* id */
    "mmc1", mod_parents,			/* name, parents */
    0x8c,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE | AW_CLK_HAS_MUX |
    AW_CLK_REPARENT);
NM_CLK(mmc2_clk,
    CLK_MMC2,					/* id */
    "mmc2", mod_parents,			/* name, parents */
    0x90,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE | AW_CLK_HAS_MUX |
    AW_CLK_REPARENT);

NM_CLK(ss_clk,
    CLK_SS,					/* id */
    "ss", mod_parents,				/* name, parents */
    0x9c,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE | AW_CLK_HAS_MUX);

NM_CLK(spi0_clk,
    CLK_SPI0,					/* id */
    "spi0", mod_parents,			/* name, parents */
    0xa0,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE | AW_CLK_HAS_MUX);
NM_CLK(spi1_clk,
    CLK_SPI1,					/* id */
    "spi1", mod_parents,			/* name, parents */
    0xa4,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE | AW_CLK_HAS_MUX);

static const char *daudio_parents[] = {"pll_audio"};
NM_CLK(i2s0_clk,
    CLK_I2S0,					/* id */
    "i2s0", daudio_parents,			/* name, parents */
    0xb0,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 4, 0, 0,					/* m factor */
    0, 0,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE);
NM_CLK(i2s1_clk,
    CLK_I2S1,					/* id */
    "i2s1", daudio_parents,			/* name, parents */
    0xb4,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 4, 0, 0,					/* m factor */
    0, 0,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE);
NM_CLK(i2s2_clk,
    CLK_I2S2,					/* id */
    "i2s2", daudio_parents,			/* name, parents */
    0xb8,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 4, 0, 0,					/* m factor */
    0, 0,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE);

static const char *tdm_parents[] = {"pll_audio"};
NM_CLK(tdm_clk,
    CLK_TDM,					/* id */
    "tdm", tdm_parents,				/* name, parents */
    0xbc,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 4, 0, 0,					/* m factor */
    0, 0,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE);

static const char *spdif_parents[] = {"pll_audio"};
NM_CLK(spdif_clk,
    CLK_SPDIF,					/* id */
    "spdif", spdif_parents,			/* name, parents */
    0xc0,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 4, 0, 0,					/* m factor */
    0, 0,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE);

static const char *dram_parents[] = {"pll_ddr"};
NM_CLK(dram_clk,
    CLK_DRAM,					/* id */
    "dram", dram_parents,			/* name, parents */
    0xf4,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 4, 0, 0,					/* m factor */
    0, 0,					/* mux */
    0,						/* gate */
    0);

static const char *tcon0_parents[] = {"pll_video0"};
MUX_CLK(tcon0_clk,
    CLK_TCON0,					/* id */
    "tcon0", tcon0_parents,			/* name, parents */
    0x118, 24, 2);				/* offset, shift, width */

static const char *tcon1_parents[] = {"pll_video1"};
NM_CLK(tcon1_clk,
    CLK_TCON1,					/* id */
    "tcon1", tcon1_parents,			/* name, parents */
    0x11c,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 4, 0, 0,					/* m factor */
    0, 0,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE);

static const char *csi_mclk_parents[] = {"pll_de", "osc24M"};
NM_CLK(csi_mclk_clk,
    CLK_CSI_MCLK,				/* id */
    "csi-mclk", csi_mclk_parents,		/* name, parents */
    0x134,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 4, 0, 0,					/* m factor */
    8, 3,					/* mux */
    15,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);

static const char *csi_sclk_parents[] = {"pll_periph", "pll_ve"};
NM_CLK(csi_sclk_clk,
    CLK_CSI_SCLK,				/* id */
    "csi-sclk", csi_sclk_parents,		/* name, parents */
    0x134,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    16, 4, 0, 0,				/* m factor */
    24, 3,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);

static const char *ve_parents[] = {"pll_ve"};
NM_CLK(ve_clk,
    CLK_VE,					/* id */
    "ve", ve_parents,				/* name, parents */
    0x13c,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    16, 3, 0, 0,				/* m factor */
    0, 0,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE);

static const char *hdmi_parents[] = {"pll_video1"};
NM_CLK(hdmi_clk,
    CLK_HDMI,					/* id */
    "hdmi", hdmi_parents,			/* name, parents */
    0x150,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);

static const char *mbus_parents[] = {"osc24M", "pll_periph", "pll_ddr"};
NM_CLK(mbus_clk,
    CLK_MBUS,					/* id */
    "mbus", mbus_parents,			/* name, parents */
    0x15c,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 3, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);

static const char *mipi_dsi0_parents[] = {"pll_video0"};
NM_CLK(mipi_dsi0_clk,
    CLK_MIPI_DSI0,				/* id */
    "mipi-dsi0", mipi_dsi0_parents,		/* name, parents */
    0x168,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 4, 0, 0,					/* m factor */
    24, 4,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);

static const char *mipi_dsi1_parents[] = {"osc24M", "pll_video0"};
NM_CLK(mipi_dsi1_clk,
    CLK_MIPI_DSI1,				/* id */
    "mipi-dsi1", mipi_dsi1_parents,		/* name, parents */
    0x16c,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 4, 0, 0,					/* m factor */
    24, 4,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);

static const char *gpu_core_parents[] = {"pll_gpu"};
NM_CLK(gpu_core_clk,
    CLK_GPU_CORE,				/* id */
    "gpu-core", gpu_core_parents,		/* name, parents */
    0x1a0,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 3, 0, 0,					/* m factor */
    0, 0,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE);

static const char *gpu_memory_parents[] = {"pll_gpu", "pll_periph"};
NM_CLK(gpu_memory_clk,
    CLK_GPU_MEMORY,				/* id */
    "gpu-memory", gpu_memory_parents,		/* name, parents */
    0x1a4,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 3, 0, 0,					/* m factor */
    24, 1,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);

static const char *gpu_hyd_parents[] = {"pll_gpu"};
NM_CLK(gpu_hyd_clk,
    CLK_GPU_HYD,				/* id */
    "gpu-hyd", gpu_hyd_parents,			/* name, parents */
    0x1a0,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 3, 0, 0,					/* m factor */
    0, 0,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE);

static struct aw_ccung_clk a83t_clks[] = {
	{ .type = AW_CLK_NKMP, .clk.nkmp = &pll_audio_clk},
	{ .type = AW_CLK_NKMP, .clk.nkmp = &pll_video0_clk},
	{ .type = AW_CLK_NKMP, .clk.nkmp = &pll_ve_clk},
	{ .type = AW_CLK_NKMP, .clk.nkmp = &pll_ddr_clk},
	{ .type = AW_CLK_NKMP, .clk.nkmp = &pll_periph_clk},
	{ .type = AW_CLK_NKMP, .clk.nkmp = &pll_gpu_clk},
	{ .type = AW_CLK_NKMP, .clk.nkmp = &pll_hsic_clk},
	{ .type = AW_CLK_NKMP, .clk.nkmp = &pll_de_clk},
	{ .type = AW_CLK_NKMP, .clk.nkmp = &pll_video1_clk},
	{ .type = AW_CLK_NKMP, .clk.nkmp = &pll_c0cpux_clk},
	{ .type = AW_CLK_NKMP, .clk.nkmp = &pll_c1cpux_clk},
	{ .type = AW_CLK_NM, .clk.nm = &apb2_clk},
	{ .type = AW_CLK_NM, .clk.nm = &nand_clk},
	{ .type = AW_CLK_NM, .clk.nm = &mmc0_clk},
	{ .type = AW_CLK_NM, .clk.nm = &mmc1_clk},
	{ .type = AW_CLK_NM, .clk.nm = &mmc2_clk},
	{ .type = AW_CLK_NM, .clk.nm = &ss_clk},
	{ .type = AW_CLK_NM, .clk.nm = &spi0_clk},
	{ .type = AW_CLK_NM, .clk.nm = &spi1_clk},
	{ .type = AW_CLK_NM, .clk.nm = &i2s0_clk},
	{ .type = AW_CLK_NM, .clk.nm = &i2s1_clk},
	{ .type = AW_CLK_NM, .clk.nm = &i2s2_clk},
	{ .type = AW_CLK_NM, .clk.nm = &tdm_clk},
	{ .type = AW_CLK_NM, .clk.nm = &spdif_clk},
	{ .type = AW_CLK_NM, .clk.nm = &dram_clk},
	{ .type = AW_CLK_NM, .clk.nm = &tcon1_clk},
	{ .type = AW_CLK_NM, .clk.nm = &csi_mclk_clk},
	{ .type = AW_CLK_NM, .clk.nm = &csi_sclk_clk},
	{ .type = AW_CLK_NM, .clk.nm = &ve_clk},
	{ .type = AW_CLK_NM, .clk.nm = &hdmi_clk},
	{ .type = AW_CLK_NM, .clk.nm = &mbus_clk},
	{ .type = AW_CLK_NM, .clk.nm = &mipi_dsi0_clk},
	{ .type = AW_CLK_NM, .clk.nm = &mipi_dsi1_clk},
	{ .type = AW_CLK_NM, .clk.nm = &gpu_core_clk},
	{ .type = AW_CLK_NM, .clk.nm = &gpu_memory_clk},
	{ .type = AW_CLK_NM, .clk.nm = &gpu_hyd_clk},
	{ .type = AW_CLK_PREDIV_MUX, .clk.prediv_mux = &ahb1_clk},
	{ .type = AW_CLK_PREDIV_MUX, .clk.prediv_mux = &ahb2_clk},
	{ .type = AW_CLK_MUX, .clk.mux = &c0cpux_clk},
	{ .type = AW_CLK_MUX, .clk.mux = &c1cpux_clk},
	{ .type = AW_CLK_MUX, .clk.mux = &cci400_clk},
	{ .type = AW_CLK_MUX, .clk.mux = &tcon0_clk},
	{ .type = AW_CLK_DIV, .clk.div = &axi0_clk},
	{ .type = AW_CLK_DIV, .clk.div = &axi1_clk},
	{ .type = AW_CLK_DIV, .clk.div = &apb1_clk},
	{ .type = AW_CLK_FIXED, .clk.fixed = &osc12m_clk},
};

static struct aw_clk_init a83t_init_clks[] = {
	{"ahb1", "pll_periph", 0, false},
	{"ahb2", "ahb1", 0, false},
	{"dram", "pll_ddr", 0, false},
};

static int
ccu_a83t_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "allwinner,sun8i-a83t-ccu"))
		return (ENXIO);

	device_set_desc(dev, "Allwinner A83T Clock Control Unit NG");
	return (BUS_PROBE_DEFAULT);
}

static int
ccu_a83t_attach(device_t dev)
{
	struct aw_ccung_softc *sc;

	sc = device_get_softc(dev);

	sc->resets = a83t_ccu_resets;
	sc->nresets = nitems(a83t_ccu_resets);
	sc->gates = a83t_ccu_gates;
	sc->ngates = nitems(a83t_ccu_gates);
	sc->clks = a83t_clks;
	sc->nclks = nitems(a83t_clks);
	sc->clk_init = a83t_init_clks;
	sc->n_clk_init = nitems(a83t_init_clks);

	return (aw_ccung_attach(dev));
}

static device_method_t ccu_a83tng_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ccu_a83t_probe),
	DEVMETHOD(device_attach,	ccu_a83t_attach),

	DEVMETHOD_END
};

static devclass_t ccu_a83tng_devclass;

DEFINE_CLASS_1(ccu_a83tng, ccu_a83tng_driver, ccu_a83tng_methods,
  sizeof(struct aw_ccung_softc), aw_ccung_driver);

EARLY_DRIVER_MODULE(ccu_a83tng, simplebus, ccu_a83tng_driver,
    ccu_a83tng_devclass, 0, 0, BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);
