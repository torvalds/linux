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

#include <dev/extres/clk/clk_div.h>
#include <dev/extres/clk/clk_fixed.h>
#include <dev/extres/clk/clk_mux.h>

#include <arm/allwinner/clkng/aw_ccung.h>

#include <dt-bindings/clock/sun6i-a31-ccu.h>
#include <dt-bindings/reset/sun6i-a31-ccu.h>

/* Non-exported clocks */
#define	CLK_PLL_CPU			0
#define	CLK_PLL_AUDIO_BASE		1
#define	CLK_PLL_AUDIO			2
#define	CLK_PLL_AUDIO_2X		3
#define	CLK_PLL_AUDIO_4X		4
#define	CLK_PLL_AUDIO_8X		5
#define	CLK_PLL_VIDEO0			6
#define	CLK_PLL_VIDEO0_2X		7
#define	CLK_PLL_VE			8
#define	CLK_PLL_DDR			9

#define	CLK_PLL_PERIPH_2X		11
#define	CLK_PLL_VIDEO1			12
#define	CLK_PLL_VIDEO1_2X		13
#define	CLK_PLL_GPU			14
#define	CLK_PLL_MIPI			15
#define	CLK_PLL9			16
#define	CLK_PLL10			17

#define	CLK_AXI				19
#define	CLK_AHB1			20
#define	CLK_APB1			21
#define	CLK_APB2			22

#define	CLK_MDFS			107
#define	CLK_SDRAM0			108
#define	CLK_SDRAM1			109

#define	CLK_MBUS0			141
#define	CLK_MBUS1			142

static struct aw_ccung_reset a31_ccu_resets[] = {
	CCU_RESET(RST_USB_PHY0, 0xcc, 0)
	CCU_RESET(RST_USB_PHY1, 0xcc, 1)
	CCU_RESET(RST_USB_PHY2, 0xcc, 2)

	CCU_RESET(RST_AHB1_MIPI_DSI, 0x2c0, 1)
	CCU_RESET(RST_AHB1_SS, 0x2c0, 5)
	CCU_RESET(RST_AHB1_DMA, 0x2c0, 6)
	CCU_RESET(RST_AHB1_MMC0, 0x2c0, 8)
	CCU_RESET(RST_AHB1_MMC1, 0x2c0, 9)
	CCU_RESET(RST_AHB1_MMC2, 0x2c0, 10)
	CCU_RESET(RST_AHB1_MMC3, 0x2c0, 11)
	CCU_RESET(RST_AHB1_NAND1, 0x2c0, 12)
	CCU_RESET(RST_AHB1_NAND0, 0x2c0, 13)
	CCU_RESET(RST_AHB1_SDRAM, 0x2c0, 14)
	CCU_RESET(RST_AHB1_EMAC, 0x2c0, 17)
	CCU_RESET(RST_AHB1_TS, 0x2c0, 18)
	CCU_RESET(RST_AHB1_HSTIMER, 0x2c0, 19)
	CCU_RESET(RST_AHB1_SPI0, 0x2c0, 20)
	CCU_RESET(RST_AHB1_SPI1, 0x2c0, 21)
	CCU_RESET(RST_AHB1_SPI2, 0x2c0, 22)
	CCU_RESET(RST_AHB1_SPI3, 0x2c0, 23)
	CCU_RESET(RST_AHB1_OTG, 0x2c0, 24)
	CCU_RESET(RST_AHB1_EHCI0, 0x2c0, 26)
	CCU_RESET(RST_AHB1_EHCI1, 0x2c0, 27)
	CCU_RESET(RST_AHB1_OHCI0, 0x2c0, 29)
	CCU_RESET(RST_AHB1_OHCI1, 0x2c0, 30)
	CCU_RESET(RST_AHB1_OHCI2, 0x2c0, 31)

	CCU_RESET(RST_AHB1_VE, 0x2c4, 0)
	CCU_RESET(RST_AHB1_LCD0, 0x2c4, 4)
	CCU_RESET(RST_AHB1_LCD1, 0x2c4, 5)
	CCU_RESET(RST_AHB1_CSI, 0x2c4, 8)
	CCU_RESET(RST_AHB1_HDMI, 0x2c4, 11)
	CCU_RESET(RST_AHB1_BE0, 0x2c4, 12)
	CCU_RESET(RST_AHB1_BE1, 0x2c4, 13)
	CCU_RESET(RST_AHB1_FE0, 0x2c4, 14)
	CCU_RESET(RST_AHB1_FE1, 0x2c4, 15)
	CCU_RESET(RST_AHB1_MP, 0x2c4, 18)
	CCU_RESET(RST_AHB1_GPU, 0x2c4, 20)
	CCU_RESET(RST_AHB1_DEU0, 0x2c4, 23)
	CCU_RESET(RST_AHB1_DEU1, 0x2c4, 24)
	CCU_RESET(RST_AHB1_DRC0, 0x2c4, 25)
	CCU_RESET(RST_AHB1_DRC1, 0x2c4, 26)

	CCU_RESET(RST_AHB1_LVDS, 0x2c8, 0)

	CCU_RESET(RST_APB1_CODEC, 0x2d0, 0)
	CCU_RESET(RST_APB1_SPDIF, 0x2d0, 1)
	CCU_RESET(RST_APB1_DIGITAL_MIC, 0x2d0, 4)
	CCU_RESET(RST_APB1_DAUDIO0, 0x2d0, 12)
	CCU_RESET(RST_APB1_DAUDIO1, 0x2d0, 13)

	CCU_RESET(RST_APB2_I2C0, 0x2d8, 0)
	CCU_RESET(RST_APB2_I2C1, 0x2d8, 1)
	CCU_RESET(RST_APB2_I2C2, 0x2d8, 2)
	CCU_RESET(RST_APB2_I2C3, 0x2d8, 3)
	CCU_RESET(RST_APB2_UART0, 0x2d8, 16)
	CCU_RESET(RST_APB2_UART1, 0x2d8, 17)
	CCU_RESET(RST_APB2_UART2, 0x2d8, 18)
	CCU_RESET(RST_APB2_UART3, 0x2d8, 19)
	CCU_RESET(RST_APB2_UART4, 0x2d8, 20)
	CCU_RESET(RST_APB2_UART5, 0x2d8, 21)
};

static struct aw_ccung_gate a31_ccu_gates[] = {
	CCU_GATE(CLK_AHB1_MIPIDSI, "ahb1-mipidsi", "ahb1", 0x60, 1)
	CCU_GATE(CLK_AHB1_SS, "ahb1-ss", "ahb1", 0x60, 5)
	CCU_GATE(CLK_AHB1_DMA, "ahb1-dma", "ahb1", 0x60, 6)
	CCU_GATE(CLK_AHB1_MMC0, "ahb1-mmc0", "ahb1", 0x60, 8)
	CCU_GATE(CLK_AHB1_MMC1, "ahb1-mmc1", "ahb1", 0x60, 9)
	CCU_GATE(CLK_AHB1_MMC2, "ahb1-mmc2", "ahb1", 0x60, 10)
	CCU_GATE(CLK_AHB1_MMC3, "ahb1-mmc3", "ahb1", 0x60, 11)
	CCU_GATE(CLK_AHB1_NAND1, "ahb1-nand1", "ahb1", 0x60, 12)
	CCU_GATE(CLK_AHB1_NAND0, "ahb1-nand0", "ahb1", 0x60, 13)
	CCU_GATE(CLK_AHB1_SDRAM, "ahb1-sdram", "ahb1", 0x60, 14)
	CCU_GATE(CLK_AHB1_EMAC, "ahb1-emac", "ahb1", 0x60, 17)
	CCU_GATE(CLK_AHB1_TS, "ahb1-ts", "ahb1", 0x60, 18)
	CCU_GATE(CLK_AHB1_HSTIMER, "ahb1-hstimer", "ahb1", 0x60, 19)
	CCU_GATE(CLK_AHB1_SPI0, "ahb1-spi0", "ahb1", 0x60, 20)
	CCU_GATE(CLK_AHB1_SPI1, "ahb1-spi1", "ahb1", 0x60, 21)
	CCU_GATE(CLK_AHB1_SPI2, "ahb1-spi2", "ahb1", 0x60, 22)
	CCU_GATE(CLK_AHB1_SPI3, "ahb1-spi3", "ahb1", 0x60, 23)
	CCU_GATE(CLK_AHB1_OTG, "ahb1-otg", "ahb1", 0x60, 24)
	CCU_GATE(CLK_AHB1_EHCI0, "ahb1-ehci0", "ahb1", 0x60, 26)
	CCU_GATE(CLK_AHB1_EHCI1, "ahb1-ehci1", "ahb1", 0x60, 27)
	CCU_GATE(CLK_AHB1_OHCI0, "ahb1-ohci0", "ahb1", 0x60, 29)
	CCU_GATE(CLK_AHB1_OHCI1, "ahb1-ohci1", "ahb1", 0x60, 30)
	CCU_GATE(CLK_AHB1_OHCI2, "ahb1-ohci2", "ahb1", 0x60, 31)
	CCU_GATE(CLK_AHB1_VE, "ahb1-ve", "ahb1", 0x64, 0)
	CCU_GATE(CLK_AHB1_LCD0, "ahb1-lcd0", "ahb1", 0x64, 4)
	CCU_GATE(CLK_AHB1_LCD1, "ahb1-lcd1", "ahb1", 0x64, 5)
	CCU_GATE(CLK_AHB1_CSI, "ahb1-csi", "ahb1", 0x64, 8)
	CCU_GATE(CLK_AHB1_HDMI, "ahb1-hdmi", "ahb1", 0x64, 11)
	CCU_GATE(CLK_AHB1_BE0, "ahb1-be0", "ahb1", 0x64, 12)
	CCU_GATE(CLK_AHB1_BE1, "ahb1-be1", "ahb1", 0x64, 13)
	CCU_GATE(CLK_AHB1_FE0, "ahb1-fe0", "ahb1", 0x64, 14)
	CCU_GATE(CLK_AHB1_FE1, "ahb1-fe1", "ahb1", 0x64, 15)
	CCU_GATE(CLK_AHB1_MP, "ahb1-mp", "ahb1", 0x64, 18)
	CCU_GATE(CLK_AHB1_GPU, "ahb1-gpu", "ahb1", 0x64, 20)
	CCU_GATE(CLK_AHB1_DEU0, "ahb1-deu0", "ahb1", 0x64, 23)
	CCU_GATE(CLK_AHB1_DEU1, "ahb1-deu1", "ahb1", 0x64, 24)
	CCU_GATE(CLK_AHB1_DRC0, "ahb1-drc0", "ahb1", 0x64, 25)
	CCU_GATE(CLK_AHB1_DRC1, "ahb1-drc1", "ahb1", 0x64, 26)

	CCU_GATE(CLK_APB1_CODEC, "apb1-codec", "apb1", 0x68, 0)
	CCU_GATE(CLK_APB1_SPDIF, "apb1-spdif", "apb1", 0x68, 1)
	CCU_GATE(CLK_APB1_DIGITAL_MIC, "apb1-digital-mic", "apb1", 0x68, 4)
	CCU_GATE(CLK_APB1_PIO, "apb1-pio", "apb1", 0x68, 5)
	CCU_GATE(CLK_APB1_DAUDIO0, "apb1-daudio0", "apb1", 0x68, 12)
	CCU_GATE(CLK_APB1_DAUDIO1, "apb1-daudio1", "apb1", 0x68, 13)

	CCU_GATE(CLK_APB2_I2C0, "apb2-i2c0", "apb2", 0x6c, 0)
	CCU_GATE(CLK_APB2_I2C1, "apb2-i2c1", "apb2", 0x6c, 1)
	CCU_GATE(CLK_APB2_I2C2, "apb2-i2c2", "apb2", 0x6c, 2)
	CCU_GATE(CLK_APB2_I2C3, "apb2-i2c3", "apb2", 0x6c, 3)
	CCU_GATE(CLK_APB2_UART0, "apb2-uart0", "apb2", 0x6c, 16)
	CCU_GATE(CLK_APB2_UART1, "apb2-uart1", "apb2", 0x6c, 17)
	CCU_GATE(CLK_APB2_UART2, "apb2-uart2", "apb2", 0x6c, 18)
	CCU_GATE(CLK_APB2_UART3, "apb2-uart3", "apb2", 0x6c, 19)
	CCU_GATE(CLK_APB2_UART4, "apb2-uart4", "apb2", 0x6c, 20)
	CCU_GATE(CLK_APB2_UART5, "apb2-uart5", "apb2", 0x6c, 21)

	CCU_GATE(CLK_DAUDIO0, "daudio0", "daudio0mux", 0xb0, 31)
	CCU_GATE(CLK_DAUDIO1, "daudio1", "daudio1mux", 0xb4, 31)

	CCU_GATE(CLK_USB_PHY0, "usb-phy0", "osc24M", 0xcc, 8)
	CCU_GATE(CLK_USB_PHY1, "usb-phy1", "osc24M", 0xcc, 9)
	CCU_GATE(CLK_USB_PHY2, "usb-phy2", "osc24M", 0xcc, 10)
	CCU_GATE(CLK_USB_OHCI0, "usb-ohci0", "osc24M", 0xcc, 16)
	CCU_GATE(CLK_USB_OHCI1, "usb-ohci1", "osc24M", 0xcc, 17)
	CCU_GATE(CLK_USB_OHCI2, "usb-ohci2", "osc24M", 0xcc, 18)

	CCU_GATE(CLK_DRAM_VE, "dram-ve", "mdfs", 0x100, 0)
	CCU_GATE(CLK_DRAM_CSI_ISP, "dram-csi_isp", "mdfs", 0x100, 1)
	CCU_GATE(CLK_DRAM_TS, "dram-ts", "mdfs", 0x100, 3)
	CCU_GATE(CLK_DRAM_DRC0, "dram-drc0", "mdfs", 0x100, 16)
	CCU_GATE(CLK_DRAM_DRC1, "dram-drc1", "mdfs", 0x100, 17)
	CCU_GATE(CLK_DRAM_DEU0, "dram-deu0", "mdfs", 0x100, 18)
	CCU_GATE(CLK_DRAM_DEU1, "dram-deu1", "mdfs", 0x100, 19)
	CCU_GATE(CLK_DRAM_FE0, "dram-fe0", "mdfs", 0x100, 24)
	CCU_GATE(CLK_DRAM_FE1, "dram-fe1", "mdfs", 0x100, 25)
	CCU_GATE(CLK_DRAM_BE0, "dram-be0", "mdfs", 0x100, 26)
	CCU_GATE(CLK_DRAM_BE1, "dram-be1", "mdfs", 0x100, 27)
	CCU_GATE(CLK_DRAM_MP, "dram-mp", "mdfs", 0x100, 28)

	CCU_GATE(CLK_CODEC, "codec", "pll_audio", 0x140, 31)

	CCU_GATE(CLK_AVS, "avs", "pll_audio", 0x144, 31)

	CCU_GATE(CLK_DIGITAL_MIC, "digital-mic", "pll_audio", 0x148, 31)

	CCU_GATE(CLK_HDMI_DDC, "hdmi-ddc", "osc24M", 0x150, 30)

	CCU_GATE(CLK_PS, "ps", "lcd1_ch1", 0x154, 31)
};

static const char *pll_parents[] = {"osc24M"};

NKMP_CLK(pll_cpu_clk,
    CLK_PLL_CPU,			/* id */
    "pll_cpu", pll_parents,			/* name, parents */
    0x00,					/* offset */
    8, 5, 0, 0,					/* n factor */
    4, 2, 0, 0,					/* k factor */
    0, 2, 0, 0,					/* m factor */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* p factor (fake) */
    31,						/* gate */
    28, 1000,					/* lock */
    AW_CLK_HAS_GATE | AW_CLK_HAS_LOCK | AW_CLK_SCALE_CHANGE);		/* flags */

NKMP_CLK(pll_audio_clk,
    CLK_PLL_AUDIO,			/* id */
    "pll_audio", pll_parents,		/* name, parents */
    0x08,					/* offset */
    8, 7, 0, 0,					/* n factor */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* k factor (fake) */
    0, 4, 1, 0,					/* m factor */
    16, 3, 1, 0,				/* p factor */
    31,						/* gate */
    28, 1000,					/* lock */
    AW_CLK_HAS_GATE | AW_CLK_HAS_LOCK);		/* flags */

static const char *pll_audio_mult_parents[] = {"pll_audio"};
FIXED_CLK(pll_audio_2x_clk,
    CLK_PLL_AUDIO_2X,		/* id */
    "pll_audio-2x",			/* name */
    pll_audio_mult_parents,		/* parent */
    0,					/* freq */
    2,					/* mult */
    1,					/* div */
    0);					/* flags */
FIXED_CLK(pll_audio_4x_clk,
    CLK_PLL_AUDIO_4X,		/* id */
    "pll_audio-4x",			/* name */
    pll_audio_mult_parents,		/* parent */
    0,					/* freq */
    4,					/* mult */
    1,					/* div */
    0);					/* flags */
FIXED_CLK(pll_audio_8x_clk,
    CLK_PLL_AUDIO_8X,		/* id */
    "pll_audio-8x",			/* name */
    pll_audio_mult_parents,		/* parent */
    0,					/* freq */
    8,					/* mult */
    1,					/* div */
    0);					/* flags */

NM_CLK_WITH_FRAC(pll_video0_clk,
    CLK_PLL_VIDEO0,				/* id */
    "pll_video0", pll_parents,		/* name, parents */
    0x10,					/* offset */
    8, 7, 0, 0,					/* n factor */
    0, 4, 0, 0,					/* m factor */
    31, 28, 1000,				/* gate, lock, lock retries */
    AW_CLK_HAS_LOCK,				/* flags */
    270000000, 297000000,			/* freq0, freq1 */
    24, 25);					/* mode sel, freq sel */

static const char *pll_video0_2x_parents[] = {"pll_video0"};
FIXED_CLK(pll_video0_2x_clk,
    CLK_PLL_VIDEO0_2X,		/* id */
    "pll_video0-2x",			/* name */
    pll_video0_2x_parents,		/* parent */
    0,					/* freq */
    2,					/* mult */
    1,					/* div */
    0);					/* flags */

NM_CLK_WITH_FRAC(pll_ve_clk,
    CLK_PLL_VE,				/* id */
    "pll_ve", pll_parents,			/* name, parents */
    0x18,					/* offset */
    8, 7, 0, 0,					/* n factor */
    0, 4, 0, 0,					/* m factor */
    31, 28, 1000,				/* gate, lock, lock retries */
    AW_CLK_HAS_LOCK,				/* flags */
    270000000, 297000000,			/* freq0, freq1 */
    24, 25);					/* mode sel, freq sel */

NKMP_CLK_WITH_UPDATE(pll_ddr_clk,
    CLK_PLL_DDR,				/* id */
    "pll_ddr", pll_parents,			/* name, parents */
    0x20,					/* offset */
    8, 5, 0, 0,					/* n factor */
    4, 2, 0, 0,					/* k factor */
    0, 2, 0, 0,					/* m factor */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* p factor (fake) */
    31,						/* gate */
    28, 1000,					/* lock */
    20,						/* update */
    AW_CLK_HAS_GATE | AW_CLK_HAS_LOCK);		/* flags */

NKMP_CLK(pll_periph_clk,
    CLK_PLL_PERIPH,			/* id */
    "pll_periph", pll_parents,		/* name, parents */
    0x28,					/* offset */
    8, 4, 0, 0,					/* n factor */
    5, 2, 1, 0,					/* k factor */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* m factor (fake) */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* p factor (fake) */
    31,						/* gate */
    28, 1000,					/* lock */
    AW_CLK_HAS_GATE | AW_CLK_HAS_LOCK);		/* flags */

static const char *pll_periph_2x_parents[] = {"pll_periph"};
FIXED_CLK(pll_periph_2x_clk,
    CLK_PLL_PERIPH_2X,	/* id */
    "pll_periph-2x",			/* name */
    pll_periph_2x_parents,		/* parent */
    0,					/* freq */
    2,					/* mult */
    1,					/* div */
    0);					/* flags */

NM_CLK_WITH_FRAC(pll_video1_clk,
    CLK_PLL_VIDEO1,				/* id */
    "pll_video1", pll_parents,		/* name, parents */
    0x30,					/* offset */
    8, 7, 0, 0,					/* n factor */
    0, 4, 0, 0,					/* m factor */
    31, 28, 1000,				/* gate, lock, lock retries */
    AW_CLK_HAS_LOCK,				/* flags */
    270000000, 297000000,			/* freq0, freq1 */
    24, 25);					/* mode sel, freq sel */

static const char *pll_video1_2x_parents[] = {"pll_video1"};
FIXED_CLK(pll_video1_2x_clk,
    CLK_PLL_VIDEO1_2X,		/* id */
    "pll_video1-2x",			/* name */
    pll_video1_2x_parents,		/* parent */
    0,					/* freq */
    2,					/* mult */
    1,					/* div */
    0);					/* flags */

NM_CLK_WITH_FRAC(pll_gpu_clk,
    CLK_PLL_GPU,				/* id */
    "pll_gpu", pll_parents,		/* name, parents */
    0x38,					/* offset */
    8, 7, 0, 0,					/* n factor */
    0, 4, 0, 0,					/* m factor */
    31, 28, 1000,				/* gate, lock, lock retries */
    AW_CLK_HAS_LOCK,				/* flags */
    270000000, 297000000,			/* freq0, freq1 */
    24, 25);					/* mode sel, freq sel */

static const char *pll_mipi_parents[] = {"pll_video0", "pll_video1"};
NKMP_CLK(pll_mipi_clk,
    CLK_PLL_MIPI,			/* id */
    "pll_mipi", pll_mipi_parents,		/* name, parents */
    0x40,					/* offset */
    8, 4, 0, 0,					/* n factor */
    4, 2, 1, 0,					/* k factor */
    0, 2, 0, 0,					/* m factor (fake) */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* p factor (fake) */
    31,						/* gate */
    28, 1000,					/* lock */
    AW_CLK_HAS_GATE | AW_CLK_HAS_LOCK);		/* flags */

NM_CLK_WITH_FRAC(pll9_clk,
    CLK_PLL9,				/* id */
    "pll9", pll_parents,		/* name, parents */
    0x44,					/* offset */
    8, 7, 0, 0,					/* n factor */
    0, 4, 0, 0,					/* m factor */
    31, 28, 1000,				/* gate, lock, lock retries */
    AW_CLK_HAS_LOCK,				/* flags */
    270000000, 297000000,			/* freq0, freq1 */
    24, 25);					/* mode sel, freq sel */

NM_CLK_WITH_FRAC(pll10_clk,
    CLK_PLL10,				/* id */
    "pll10", pll_parents,		/* name, parents */
    0x48,					/* offset */
    8, 7, 0, 0,					/* n factor */
    0, 4, 0, 0,					/* m factor */
    31, 28, 1000,				/* gate, lock, lock retries */
    AW_CLK_HAS_LOCK,				/* flags */
    270000000, 297000000,			/* freq0, freq1 */
    24, 25);					/* mode sel, freq sel */

static struct clk_div_table axi_div_table[] = {
	{ .value = 0, .divider = 1, },
	{ .value = 1, .divider = 2, },
	{ .value = 2, .divider = 3, },
	{ .value = 3, .divider = 4, },
	{ .value = 4, .divider = 4, },
	{ .value = 5, .divider = 4, },
	{ .value = 6, .divider = 4, },
	{ .value = 7, .divider = 4, },
	{ },
};
static const char *axi_parents[] = {"cpu"};
DIV_CLK(axi_clk,
    CLK_AXI,		/* id */
    "axi", axi_parents,		/* name, parents */
    0x50,			/* offset */
    0, 2,			/* shift, mask */
    0, axi_div_table);		/* flags, div table */

static const char *cpu_parents[] = {"osc32k", "osc24M", "pll_cpu", "pll_cpu"};
MUX_CLK(cpu_clk,
    CLK_CPU,		/* id */
    "cpu", cpu_parents,		/* name, parents */
    0x50, 16, 2);		/* offset, shift, width */

static const char *ahb1_parents[] = {"osc32k", "osc24M", "axi", "pll_periph"};
PREDIV_CLK(ahb1_clk,
    CLK_AHB1,					/* id */
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
    CLK_APB1,		/* id */
    "apb1", apb1_parents,	/* name, parents */
    0x54,			/* offset */
    8, 2,			/* shift, mask */
    CLK_DIV_WITH_TABLE,		/* flags */
    apb1_div_table);		/* div table */

static const char *apb2_parents[] = {"osc32k", "osc24M", "pll_periph", "pll_periph"};
NM_CLK(apb2_clk,
    CLK_APB2,				/* id */
    "apb2", apb2_parents,			/* name, parents */
    0x58,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 5, 0, 0,					/* m factor */
    24, 2,					/* mux */
    0,						/* gate */
    AW_CLK_HAS_MUX);

static const char *mod_parents[] = {"osc24M", "pll_periph"};
NM_CLK(nand0_clk,
    CLK_NAND0, "nand0", mod_parents,	/* id, name, parents */
    0x80,					/* offset */
    16, 3, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE | AW_CLK_HAS_MUX);		/* flags */

NM_CLK(nand1_clk,
    CLK_NAND1, "nand1", mod_parents,	/* id, name, parents */
    0x80,					/* offset */
    16, 3, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE | AW_CLK_HAS_MUX);		/* flags */

NM_CLK(mmc0_clk,
    CLK_MMC0, "mmc0", mod_parents,	/* id, name, parents */
    0x88,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE | AW_CLK_HAS_MUX |
    AW_CLK_REPARENT);				/* flags */

NM_CLK(mmc1_clk,
    CLK_MMC1, "mmc1", mod_parents,	/* id, name, parents */
    0x8c,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE | AW_CLK_HAS_MUX |
    AW_CLK_REPARENT);				/* flags */

NM_CLK(mmc2_clk,
    CLK_MMC2, "mmc2", mod_parents,	/* id, name, parents */
    0x90,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE | AW_CLK_HAS_MUX |
    AW_CLK_REPARENT);				/* flags */

NM_CLK(mmc3_clk,
    CLK_MMC2, "mmc3", mod_parents,	/* id, name, parents */
    0x94,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE | AW_CLK_HAS_MUX |
    AW_CLK_REPARENT);				/* flags */

static const char *ts_parents[] = {"osc24M", "pll_periph"};
NM_CLK(ts_clk,
    CLK_TS, "ts", ts_parents,		/* id, name, parents */
    0x98,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 4,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE | AW_CLK_HAS_MUX);		/* flags */

NM_CLK(ss_clk,
    CLK_SS, "ss", mod_parents,		/* id, name, parents */
    0x9C,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 4,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE | AW_CLK_HAS_MUX);		/* flags */

NM_CLK(spi0_clk,
    CLK_SPI0, "spi0", mod_parents,	/* id, name, parents */
    0xA0,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 4,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE | AW_CLK_HAS_MUX);		/* flags */

NM_CLK(spi1_clk,
    CLK_SPI1, "spi1", mod_parents,	/* id, name, parents */
    0xA4,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 4,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE | AW_CLK_HAS_MUX);		/* flags */

NM_CLK(spi2_clk,
    CLK_SPI2, "spi2", mod_parents,	/* id, name, parents */
    0xA8,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 4,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE | AW_CLK_HAS_MUX);		/* flags */

NM_CLK(spi3_clk,
    CLK_SPI3, "spi3", mod_parents,	/* id, name, parents */
    0xAC,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 4,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE | AW_CLK_HAS_MUX);		/* flags */


static const char *daudio_parents[] = {"pll_audio-8x", "pll_audio-4x", "pll_audio-2x", "pll_audio"};
MUX_CLK(daudio0mux_clk,
    0,
    "daudio0mux", daudio_parents,
    0xb0, 16, 2);
MUX_CLK(daudio1mux_clk,
    0,
    "daudio1mux", daudio_parents,
    0xb4, 16, 2);

static const char *mdfs_parents[] = {"pll_ddr", "pll_periph"};
NM_CLK(mdfs_clk,
    CLK_MDFS, "mdfs", mdfs_parents,	/* id, name, parents */
    0xF0,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 4,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE | AW_CLK_HAS_MUX);		/* flags */

static const char *dram_parents[] = {"pll_ddr", "pll_periph"};
NM_CLK(sdram0_clk,
    CLK_SDRAM0, "sdram0", dram_parents,	/* id, name, parents */
    0xF4,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 4, 0, 0,					/* m factor */
    4, 1,					/* mux */
    0,						/* gate */
    AW_CLK_HAS_MUX);				/* flags */
NM_CLK(sdram1_clk,
    CLK_SDRAM1, "sdram1", dram_parents,	/* id, name, parents */
    0xF4,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    8, 4, 0, 0,					/* m factor */
    12, 1,					/* mux */
    0,						/* gate */
    AW_CLK_HAS_MUX);				/* flags */

static const char *befe_parents[] = {"pll_video0", "pll_video1", "pll_periph-2x", "pll_gpu", "pll9", "pll10"};
NM_CLK(be0_clk,
    CLK_BE0, "be0", befe_parents,	/* id, name, parents */
    0x104,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 4, 0, 0,					/* m factor */
    24, 3,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

NM_CLK(be1_clk,
    CLK_BE1, "be1", befe_parents,	/* id, name, parents */
    0x108,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 4, 0, 0,					/* m factor */
    24, 3,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

NM_CLK(fe0_clk,
    CLK_FE0, "fe0", befe_parents,	/* id, name, parents */
    0x104,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 4, 0, 0,					/* m factor */
    24, 3,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */
NM_CLK(fe1_clk,
    CLK_FE1, "fe1", befe_parents,	/* id, name, parents */
    0x108,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 4, 0, 0,					/* m factor */
    24, 3,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

static const char *mp_parents[] = {"pll_video0", "pll_video1", "pll9", "pll10"};
NM_CLK(mp_clk,
    CLK_MP, "mp", mp_parents,	/* id, name, parents */
    0x108,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 4, 0, 0,					/* m factor */
    24, 3,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

static const char *lcd_ch0_parents[] = {"pll_video0", "pll_video1", "pll_video0-2x", "pll_video1-2x", "pll_mipi"};
NM_CLK(lcd0_ch0_clk,
    CLK_LCD0_CH0, "lcd0_ch0", lcd_ch0_parents,	/* id, name, parents */
    0x118,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 0, 1, AW_CLK_FACTOR_FIXED,					/* m factor (fake )*/
    24, 3,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

NM_CLK(lcd1_ch0_clk,
    CLK_LCD1_CH0, "lcd1_ch0", lcd_ch0_parents,	/* id, name, parents */
    0x11C,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 0, 1, AW_CLK_FACTOR_FIXED,					/* m factor (fake )*/
    24, 3,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

static const char *lcd_ch1_parents[] = {"pll_video0", "pll_video1", "pll_video0-2x", "pll_video1-2x"};
NM_CLK(lcd0_ch1_clk,
    CLK_LCD0_CH1, "lcd0_ch1", lcd_ch1_parents,	/* id, name, parents */
    0x12C,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 4, 0, 0,					/* m factor */
    24, 3,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

NM_CLK(lcd1_ch1_clk,
    CLK_LCD1_CH1, "lcd1_ch1", lcd_ch1_parents,	/* id, name, parents */
    0x130,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 4, 0, 0,					/* m factor */
    24, 3,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

/* CSI0 0x134 Need Mux table */
/* CSI1 0x138 Need Mux table */

static const char *ve_parents[] = {"pll_ve"};
NM_CLK(ve_clk,
    CLK_VE, "ve", ve_parents,		/* id, name, parents */
    0x13C,					/* offset */
    16, 3, 0, 0,				/* n factor */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* m factor (fake) */
    0, 0,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE);				/* flags */

NM_CLK(hdmi_clk,
    CLK_HDMI, "hdmi", lcd_ch1_parents,	/* id, name, parents */
    0x150,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 4, 0, 0,					/* m factor */
    0, 0,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_GATE);				/* flags */

static const char *mbus_parents[] = {"osc24M", "pll_periph", "pll_ddr"};
NM_CLK(mbus0_clk,
    CLK_MBUS0, "mbus0", mbus_parents,	/* id, name, parents */
    0x15C,					/* offset */
    16, 2, 0, 0,				/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

NM_CLK(mbus1_clk,
    CLK_MBUS1, "mbus1", mbus_parents,	/* id, name, parents */
    0x160,					/* offset */
    16, 2, 0, 0,				/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

static const char *mipi_parents[] = {"pll_video0", "pll_video1", "pll_video0-2x", "pll_video1-2x"};
NM_CLK(mipi_dsi_clk,
    CLK_MIPI_DSI, "mipi_dsi", mipi_parents,	/* id, name, parents */
    0x168,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    16, 4, 0, 0,				/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

NM_CLK(mipi_dsi_dphy_clk,
    CLK_MIPI_DSI_DPHY, "mipi_dsi_dphy", mipi_parents,	/* id, name, parents */
    0x168,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 4, 0, 0,					/* m factor */
    8, 2,					/* mux */
    15,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

NM_CLK(mipi_csi_dphy_clk,
    CLK_MIPI_CSI_DPHY, "mipi_csi_dphy", mipi_parents,	/* id, name, parents */
    0x16C,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 4, 0, 0,					/* m factor */
    8, 2,					/* mux */
    15,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

static const char *iep_parents[] = {"pll_video0", "pll_video1", "pll_periph-2x", "pll_gpu", "pll9", "pll10"};

NM_CLK(iep_drc0_clk,
    CLK_IEP_DRC0, "iep_drc0", iep_parents,	/* id, name, parents */
    0x180,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

NM_CLK(iep_drc1_clk,
    CLK_IEP_DRC1, "iep_drc1", iep_parents,	/* id, name, parents */
    0x184,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

NM_CLK(iep_deu0_clk,
    CLK_IEP_DEU0, "iep_deu0", iep_parents,	/* id, name, parents */
    0x188,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

NM_CLK(iep_deu1_clk,
    CLK_IEP_DEU1, "iep_deu1", iep_parents,	/* id, name, parents */
    0x18C,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

static const char *gpu_parents[] = {"pll_gpu", "pll_periph-2x", "pll_video0", "pll_video1", "pll9", "pll10"};
PREDIV_CLK(gpu_core_clk,
    CLK_GPU_CORE,				/* id */
    "gpu_core", gpu_parents,		/* name, parents */
    0x1A0,					/* offset */
    24, 3,					/* mux */
    0, 3, 0, 0,					/* div */
    0, 0, 3, AW_CLK_FACTOR_HAS_COND | AW_CLK_FACTOR_FIXED,	/* prediv */
    24, 2, 1);					/* prediv condition */

PREDIV_CLK(gpu_memory_clk,
    CLK_GPU_MEMORY,				/* id */
    "gpu_memory", gpu_parents,			/* name, parents */
    0x1A4,					/* offset */
    24, 3,					/* mux */
    0, 3, 0, 0,					/* div */
    0, 0, 3, AW_CLK_FACTOR_HAS_COND | AW_CLK_FACTOR_FIXED,	/* prediv */
    24, 2, 1);					/* prediv condition */

PREDIV_CLK(gpu_hyd_clk,
    CLK_GPU_HYD,				/* id */
    "gpu_hyd", gpu_parents,			/* name, parents */
    0x1A8,					/* offset */
    24, 3,					/* mux */
    0, 3, 0, 0,					/* div */
    0, 0, 3, AW_CLK_FACTOR_HAS_COND | AW_CLK_FACTOR_FIXED,	/* prediv */
    24, 2, 1);					/* prediv condition */

/* ATS 0x1B0 */
/* Trace 0x1B4 */
static struct aw_ccung_clk a31_ccu_clks[] = {
	{ .type = AW_CLK_NKMP, .clk.nkmp = &pll_cpu_clk},
	{ .type = AW_CLK_NKMP, .clk.nkmp = &pll_audio_clk},
	{ .type = AW_CLK_NKMP, .clk.nkmp = &pll_periph_clk},
	{ .type = AW_CLK_NKMP, .clk.nkmp = &pll_ddr_clk},
	{ .type = AW_CLK_NKMP, .clk.nkmp = &pll_mipi_clk},
	{ .type = AW_CLK_NM, .clk.nm = &pll_video0_clk},
	{ .type = AW_CLK_NM, .clk.nm = &pll_ve_clk},
	{ .type = AW_CLK_NM, .clk.nm = &pll_video1_clk},
	{ .type = AW_CLK_NM, .clk.nm = &pll_gpu_clk},
	{ .type = AW_CLK_NM, .clk.nm = &pll9_clk},
	{ .type = AW_CLK_NM, .clk.nm = &pll10_clk},
	{ .type = AW_CLK_NM, .clk.nm = &apb2_clk},
	{ .type = AW_CLK_NM, .clk.nm = &nand0_clk},
	{ .type = AW_CLK_NM, .clk.nm = &nand1_clk},
	{ .type = AW_CLK_NM, .clk.nm = &mmc0_clk},
	{ .type = AW_CLK_NM, .clk.nm = &mmc1_clk},
	{ .type = AW_CLK_NM, .clk.nm = &mmc2_clk},
	{ .type = AW_CLK_NM, .clk.nm = &mmc3_clk},
	{ .type = AW_CLK_NM, .clk.nm = &ts_clk},
	{ .type = AW_CLK_NM, .clk.nm = &ss_clk},
	{ .type = AW_CLK_NM, .clk.nm = &spi0_clk},
	{ .type = AW_CLK_NM, .clk.nm = &spi1_clk},
	{ .type = AW_CLK_NM, .clk.nm = &spi2_clk},
	{ .type = AW_CLK_NM, .clk.nm = &spi3_clk},
	{ .type = AW_CLK_NM, .clk.nm = &mdfs_clk},
	{ .type = AW_CLK_NM, .clk.nm = &sdram0_clk},
	{ .type = AW_CLK_NM, .clk.nm = &sdram1_clk},
	{ .type = AW_CLK_NM, .clk.nm = &be0_clk},
	{ .type = AW_CLK_NM, .clk.nm = &be1_clk},
	{ .type = AW_CLK_NM, .clk.nm = &fe0_clk},
	{ .type = AW_CLK_NM, .clk.nm = &fe1_clk},
	{ .type = AW_CLK_NM, .clk.nm = &mp_clk},
	{ .type = AW_CLK_NM, .clk.nm = &lcd0_ch0_clk},
	{ .type = AW_CLK_NM, .clk.nm = &lcd1_ch0_clk},
	{ .type = AW_CLK_NM, .clk.nm = &lcd0_ch1_clk},
	{ .type = AW_CLK_NM, .clk.nm = &lcd1_ch1_clk},
	{ .type = AW_CLK_NM, .clk.nm = &ve_clk},
	{ .type = AW_CLK_NM, .clk.nm = &hdmi_clk},
	{ .type = AW_CLK_NM, .clk.nm = &mbus0_clk},
	{ .type = AW_CLK_NM, .clk.nm = &mbus1_clk},
	{ .type = AW_CLK_NM, .clk.nm = &mipi_dsi_clk},
	{ .type = AW_CLK_NM, .clk.nm = &mipi_dsi_dphy_clk},
	{ .type = AW_CLK_NM, .clk.nm = &mipi_csi_dphy_clk},
	{ .type = AW_CLK_NM, .clk.nm = &iep_drc0_clk},
	{ .type = AW_CLK_NM, .clk.nm = &iep_drc1_clk},
	{ .type = AW_CLK_NM, .clk.nm = &iep_deu0_clk},
	{ .type = AW_CLK_NM, .clk.nm = &iep_deu1_clk},
	{ .type = AW_CLK_PREDIV_MUX, .clk.prediv_mux = &ahb1_clk},
	{ .type = AW_CLK_PREDIV_MUX, .clk.prediv_mux = &gpu_core_clk},
	{ .type = AW_CLK_PREDIV_MUX, .clk.prediv_mux = &gpu_memory_clk},
	{ .type = AW_CLK_PREDIV_MUX, .clk.prediv_mux = &gpu_hyd_clk},
	{ .type = AW_CLK_DIV, .clk.div = &axi_clk},
	{ .type = AW_CLK_DIV, .clk.div = &apb1_clk},
	{ .type = AW_CLK_MUX, .clk.mux = &cpu_clk},
	{ .type = AW_CLK_MUX, .clk.mux = &daudio0mux_clk},
	{ .type = AW_CLK_MUX, .clk.mux = &daudio1mux_clk},
	{ .type = AW_CLK_FIXED, .clk.fixed = &pll_audio_2x_clk},
	{ .type = AW_CLK_FIXED, .clk.fixed = &pll_audio_4x_clk},
	{ .type = AW_CLK_FIXED, .clk.fixed = &pll_audio_8x_clk},
	{ .type = AW_CLK_FIXED, .clk.fixed = &pll_video0_2x_clk},
	{ .type = AW_CLK_FIXED, .clk.fixed = &pll_periph_2x_clk},
	{ .type = AW_CLK_FIXED, .clk.fixed = &pll_video1_2x_clk},
};

static int
ccu_a31_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "allwinner,sun6i-a31-ccu"))
		return (ENXIO);

	device_set_desc(dev, "Allwinner A31 Clock Control Unit NG");
	return (BUS_PROBE_DEFAULT);
}

static int
ccu_a31_attach(device_t dev)
{
	struct aw_ccung_softc *sc;

	sc = device_get_softc(dev);

	sc->resets = a31_ccu_resets;
	sc->nresets = nitems(a31_ccu_resets);
	sc->gates = a31_ccu_gates;
	sc->ngates = nitems(a31_ccu_gates);
	sc->clks = a31_ccu_clks;
	sc->nclks = nitems(a31_ccu_clks);

	return (aw_ccung_attach(dev));
}

static device_method_t ccu_a31ng_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ccu_a31_probe),
	DEVMETHOD(device_attach,	ccu_a31_attach),

	DEVMETHOD_END
};

static devclass_t ccu_a31ng_devclass;

DEFINE_CLASS_1(ccu_a31ng, ccu_a31ng_driver, ccu_a31ng_methods,
  sizeof(struct aw_ccung_softc), aw_ccung_driver);

EARLY_DRIVER_MODULE(ccu_a31ng, simplebus, ccu_a31ng_driver,
    ccu_a31ng_devclass, 0, 0, BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);
