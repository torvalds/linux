/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Kyle Evans <kevans@FreeBSD.org>
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

#include <gnu/dts/include/dt-bindings/clock/sun4i-a10-ccu.h>
#include <gnu/dts/include/dt-bindings/clock/sun7i-a20-ccu.h>
#include <gnu/dts/include/dt-bindings/reset/sun4i-a10-ccu.h>

/* Non-exported resets */
/* Non-exported clocks */
#define	CLK_PLL_CORE		2
#define	CLK_AXI			3
#define	CLK_AHB			4
#define	CLK_APB0		5
#define	CLK_APB1		6
#define	CLK_PLL_VIDEO0		8
#define	CLK_PLL_DDR		12
#define	CLK_PLL_DDR_OTHER	13
#define	CLK_PLL6		14
#define	CLK_PLL_PERIPH		15
#define	CLK_PLL_SATA		16
#define	CLK_PLL_VIDEO1		17

/* Non-exported fixed clocks */

static struct aw_ccung_reset a10_ccu_resets[] = {
	CCU_RESET(RST_USB_PHY0, 0xcc, 0)
	CCU_RESET(RST_USB_PHY1, 0xcc, 1)
	CCU_RESET(RST_USB_PHY2, 0xcc, 2)

	CCU_RESET(RST_GPS, 0xd0, 0)

	CCU_RESET(RST_DE_BE0, 0x104, 30)
	CCU_RESET(RST_DE_BE1, 0x108, 30)
	CCU_RESET(RST_DE_FE0, 0x10c, 30)
	CCU_RESET(RST_DE_FE1, 0x110, 30)
	CCU_RESET(RST_DE_MP, 0x114, 30)

	CCU_RESET(RST_TVE0, 0x118, 29)
	CCU_RESET(RST_TCON0, 0x118, 30)

	CCU_RESET(RST_TVE1, 0x11c, 29)
	CCU_RESET(RST_TCON1, 0x11c, 30)

	CCU_RESET(RST_CSI0, 0x134, 30)
	CCU_RESET(RST_CSI1, 0x138, 30)

	CCU_RESET(RST_VE, 0x13c, 0)

	CCU_RESET(RST_ACE, 0x148, 16)

	CCU_RESET(RST_LVDS, 0x14c, 0)

	CCU_RESET(RST_GPU, 0x154, 30)

	CCU_RESET(RST_HDMI_H, 0x170, 0)
	CCU_RESET(RST_HDMI_SYS, 0x170, 1)
	CCU_RESET(RST_HDMI_AUDIO_DMA, 0x170, 2)
};

static struct aw_ccung_gate a10_ccu_gates[] = {
	CCU_GATE(CLK_HOSC, "hosc", "osc24M", 0x50, 0)

	CCU_GATE(CLK_AHB_OTG, "ahb-otg", "ahb", 0x60, 0)
	CCU_GATE(CLK_AHB_EHCI0, "ahb-ehci0", "ahb", 0x60, 1)
	CCU_GATE(CLK_AHB_OHCI0, "ahb-ohci0", "ahb", 0x60, 2)
	CCU_GATE(CLK_AHB_EHCI1, "ahb-ehci1", "ahb", 0x60, 3)
	CCU_GATE(CLK_AHB_OHCI1, "ahb-ohci1", "ahb", 0x60, 4)
	CCU_GATE(CLK_AHB_SS, "ahb-ss", "ahb", 0x60, 5)
	CCU_GATE(CLK_AHB_DMA, "ahb-dma", "ahb", 0x60, 6)
	CCU_GATE(CLK_AHB_BIST, "ahb-bist", "ahb", 0x60, 7)
	CCU_GATE(CLK_AHB_MMC0, "ahb-mmc0", "ahb", 0x60, 8)
	CCU_GATE(CLK_AHB_MMC1, "ahb-mmc1", "ahb", 0x60, 9)
	CCU_GATE(CLK_AHB_MMC2, "ahb-mmc2", "ahb", 0x60, 10)
	CCU_GATE(CLK_AHB_MMC3, "ahb-mmc3", "ahb", 0x60, 11)
	CCU_GATE(CLK_AHB_MS, "ahb-ms", "ahb", 0x60, 12)
	CCU_GATE(CLK_AHB_NAND, "ahb-nand", "ahb", 0x60, 13)
	CCU_GATE(CLK_AHB_SDRAM, "ahb-sdram", "ahb", 0x60, 14)
	CCU_GATE(CLK_AHB_ACE, "ahb-ace", "ahb", 0x60, 16)
	CCU_GATE(CLK_AHB_EMAC, "ahb-emac", "ahb", 0x60, 17)
	CCU_GATE(CLK_AHB_TS, "ahb-ts", "ahb", 0x60, 18)
	CCU_GATE(CLK_AHB_SPI0, "ahb-spi0", "ahb", 0x60, 20)
	CCU_GATE(CLK_AHB_SPI1, "ahb-spi1", "ahb", 0x60, 21)
	CCU_GATE(CLK_AHB_SPI2, "ahb-spi2", "ahb", 0x60, 22)
	CCU_GATE(CLK_AHB_SPI3, "ahb-spi3", "ahb", 0x60, 23)
	CCU_GATE(CLK_AHB_SATA, "ahb-sata", "ahb", 0x60, 25)

	CCU_GATE(CLK_AHB_VE, "ahb-ve", "ahb", 0x64, 0)
	CCU_GATE(CLK_AHB_TVD, "ahb-tvd", "ahb", 0x64, 1)
	CCU_GATE(CLK_AHB_TVE0, "ahb-tve0", "ahb", 0x64, 2)
	CCU_GATE(CLK_AHB_TVE1, "ahb-tve1", "ahb", 0x64, 3)
	CCU_GATE(CLK_AHB_LCD0, "ahb-lcd0", "ahb", 0x64, 4)
	CCU_GATE(CLK_AHB_LCD1, "ahb-lcd1", "ahb", 0x64, 5)
	CCU_GATE(CLK_AHB_CSI0, "ahb-csi0", "ahb", 0x64, 8)
	CCU_GATE(CLK_AHB_CSI1, "ahb-csi1", "ahb", 0x64, 9)
	CCU_GATE(CLK_AHB_HDMI1, "ahb-hdmi1", "ahb", 0x64, 10)
	CCU_GATE(CLK_AHB_HDMI0, "ahb-hdmi0", "ahb", 0x64, 11)
	CCU_GATE(CLK_AHB_DE_BE0, "ahb-de_be0", "ahb", 0x64, 12)
	CCU_GATE(CLK_AHB_DE_BE1, "ahb-de_be1", "ahb", 0x64, 13)
	CCU_GATE(CLK_AHB_DE_FE0, "ahb-de_fe0", "ahb", 0x64, 14)
	CCU_GATE(CLK_AHB_DE_FE1, "ahb-de_fe1", "ahb", 0x64, 15)
	CCU_GATE(CLK_AHB_GMAC, "ahb-gmac", "ahb", 0x64, 17)
	CCU_GATE(CLK_AHB_MP, "ahb-mp", "ahb", 0x64, 18)
	CCU_GATE(CLK_AHB_GPU, "ahb-gpu", "ahb", 0x64, 20)

	CCU_GATE(CLK_APB0_CODEC, "apb0-codec", "apb0", 0x68, 0)
	CCU_GATE(CLK_APB0_SPDIF, "apb0-spdif", "apb0", 0x68, 1)
	CCU_GATE(CLK_APB0_AC97, "apb0-ac97", "apb0", 0x68, 2)
	CCU_GATE(CLK_APB0_I2S0, "apb0-i2s0", "apb0", 0x68, 3)
	CCU_GATE(CLK_APB0_I2S1, "apb0-i2s1", "apb0", 0x68, 4)
	CCU_GATE(CLK_APB0_PIO, "apb0-pi0", "apb0", 0x68, 5)
	CCU_GATE(CLK_APB0_IR0, "apb0-ir0", "apb0", 0x68, 6)
	CCU_GATE(CLK_APB0_IR1, "apb0-ir1", "apb0", 0x68, 7)
	CCU_GATE(CLK_APB0_I2S2, "apb0-i2s2", "apb0",0x68, 8)
	CCU_GATE(CLK_APB0_KEYPAD, "apb0-keypad", "apb0", 0x68, 10)

	CCU_GATE(CLK_APB1_I2C0, "apb1-i2c0", "apb1", 0x6c, 0)
	CCU_GATE(CLK_APB1_I2C1, "apb1-i2c1", "apb1",0x6c, 1)
	CCU_GATE(CLK_APB1_I2C2, "apb1-i2c2", "apb1",0x6c, 2)
	CCU_GATE(CLK_APB1_I2C3, "apb1-i2c3", "apb1",0x6c, 3)
	CCU_GATE(CLK_APB1_CAN, "apb1-can", "apb1",0x6c, 4)
	CCU_GATE(CLK_APB1_SCR, "apb1-scr", "apb1",0x6c, 5)
	CCU_GATE(CLK_APB1_PS20, "apb1-ps20", "apb1",0x6c, 6)
	CCU_GATE(CLK_APB1_PS21, "apb1-ps21", "apb1",0x6c, 7)
	CCU_GATE(CLK_APB1_I2C4, "apb1-i2c4", "apb1", 0x6c, 15)
	CCU_GATE(CLK_APB1_UART0, "apb1-uart0", "apb1",0x6c, 16)
	CCU_GATE(CLK_APB1_UART1, "apb1-uart1", "apb1",0x6c, 17)
	CCU_GATE(CLK_APB1_UART2, "apb1-uart2", "apb1",0x6c, 18)
	CCU_GATE(CLK_APB1_UART3, "apb1-uart3", "apb1",0x6c, 19)
	CCU_GATE(CLK_APB1_UART4, "apb1-uart4", "apb1",0x6c, 20)
	CCU_GATE(CLK_APB1_UART5, "apb1-uart5", "apb1",0x6c, 21)
	CCU_GATE(CLK_APB1_UART6, "apb1-uart6", "apb1",0x6c, 22)
	CCU_GATE(CLK_APB1_UART7, "apb1-uart7", "apb1",0x6c, 23)

	CCU_GATE(CLK_USB_OHCI0, "usb-ohci0", "ahb", 0xcc, 6)
	CCU_GATE(CLK_USB_OHCI1, "usb-ohci1", "ahb", 0xcc, 7)
	CCU_GATE(CLK_USB_PHY, "usb-phy", "ahb", 0xcc, 8)

	CCU_GATE(CLK_DRAM_VE, "dram-ve", "pll_ddr", 0x100, 0)
	CCU_GATE(CLK_DRAM_CSI0, "dram-csi0", "pll_ddr", 0x100, 1)
	CCU_GATE(CLK_DRAM_CSI1, "dram-csi1", "pll_ddr", 0x100, 2)
	CCU_GATE(CLK_DRAM_TS, "dram-ts", "pll_ddr", 0x100, 3)
	CCU_GATE(CLK_DRAM_TVD, "dram-tvd", "pll_ddr", 0x100, 4)
	CCU_GATE(CLK_DRAM_TVE0, "dram-tve0", "pll_ddr", 0x100, 5)
	CCU_GATE(CLK_DRAM_TVE1, "dram-tve1", "pll_ddr", 0x100, 6)
	CCU_GATE(CLK_DRAM_OUT, "dram-out", "pll_ddr", 0x100, 15)
	CCU_GATE(CLK_DRAM_DE_FE1, "dram-de_fe1", "pll_ddr", 0x100, 24)
	CCU_GATE(CLK_DRAM_DE_FE0, "dram-de_fe0", "pll_ddr", 0x100, 25)
	CCU_GATE(CLK_DRAM_DE_BE0, "dram-de_be0", "pll_ddr", 0x100, 26)
	CCU_GATE(CLK_DRAM_DE_BE1, "dram-de_be1", "pll_ddr", 0x100, 27)
	CCU_GATE(CLK_DRAM_MP, "dram-de_mp", "pll_ddr", 0x100, 28)
	CCU_GATE(CLK_DRAM_ACE, "dram-ace", "pll_ddr", 0x100, 29)
};

static const char *pll_parents[] = {"osc24M"};
NKMP_CLK(pll_core_clk,
    CLK_PLL_CORE,				/* id */
    "pll_core", pll_parents,			/* name, parents */
    0x00,					/* offset */
    8, 5, 0, AW_CLK_FACTOR_ZERO_IS_ONE,		/* n factor */
    4, 2, 0, 0,					/* k factor */
    0, 2, 0, 0,					/* m factor */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* p factor */
    31,						/* gate */
    0, 0,					/* lock */
    AW_CLK_HAS_GATE);				/* flags */

NM_CLK_WITH_FRAC(pll_video0_clk,
    CLK_PLL_VIDEO0,				/* id */
    "pll_video0", pll_parents,			/* name, parents */
    0x10,					/* offset */
    0, 7, 0, 0,					/* n factor */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* m factor (fake) */
    31, 0, 0,					/* gate, lock, lock retries */
    AW_CLK_HAS_GATE,				/* flags */
    270000000, 297000000,			/* freq0, freq1 */
    15, 14);					/* mode sel, freq sel */
static const char *pll_video0_2x_parents[] = {"pll_video0"};
FIXED_CLK(pll_video0_2x_clk,
    CLK_PLL_VIDEO0_2X,				/* id */
    "pll_video0-2x", pll_video0_2x_parents,	/* name, parents */
    0,						/* freq */
    2,						/* mult */
    1,						/* div */
    0);						/* flags */

NM_CLK_WITH_FRAC(pll_video1_clk,
    CLK_PLL_VIDEO1,				/* id */
    "pll_video1", pll_parents,			/* name, parents */
    0x30,					/* offset */
    0, 7, 0, 0,					/* n factor */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* m factor (fake) */
    31, 0, 0,					/* gate, lock, lock retries */
    AW_CLK_HAS_GATE,				/* flags */
    270000000, 297000000,			/* freq0, freq1 */
    15, 14);					/* mode sel, freq sel */
static const char *pll_video1_2x_parents[] = {"pll_video1"};
FIXED_CLK(pll_video1_2x_clk,
    CLK_PLL_VIDEO1_2X,				/* id */
    "pll_video1-2x", pll_video1_2x_parents,	/* name, parents */
    0,						/* freq */
    2,						/* mult */
    1,						/* div */
    0);						/* flags */

static const char *cpu_parents[] = {"osc32k", "osc24M", "pll_core", "pll_periph"};
static const char *axi_parents[] = {"cpu"};
static const char *ahb_parents[] = {"axi", "pll_periph", "pll6"};
static const char *apb0_parents[] = {"ahb"};
static const char *apb1_parents[] = {"osc24M", "pll_periph", "osc32k"};
MUX_CLK(cpu_clk,
    CLK_CPU,					/* id */
    "cpu", cpu_parents,				/* name, parents */
    0x54, 16, 2);				/* offset, shift, width */
NM_CLK(axi_clk,
    CLK_AXI,					/* id */
    "axi", axi_parents,				/* name, parents */
    0x54,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 2, 0, 0,					/* m factor */
    0, 0,					/* mux */
    0,						/* gate */
    0);						/* flags */
NM_CLK(ahb_clk,
    CLK_AHB,					/* id */
    "ahb", ahb_parents,				/* name, parents */
    0x54,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    4, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* m factor */
    6, 2,					/* mux */
    0,						/* gate */
    AW_CLK_HAS_MUX);				/* flags */
NM_CLK(apb0_clk,
    CLK_APB0,					/* id */
    "apb0", apb0_parents,			/* name, parents */
    0x54,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    8, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO |
    AW_CLK_FACTOR_ZERO_IS_ONE,			/* m factor */
    0, 0,					/* mux */
    0,						/* gate */
    0);						/* flags */

NM_CLK(apb1_clk,
    CLK_APB1,					/* id */
    "apb1", apb1_parents,			/* name, parents */
    0x58,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 5, 0, 0,					/* m factor */
    24, 2,					/* mux */
    0,						/* gate */
    AW_CLK_HAS_MUX);				/* flags */


NKMP_CLK(pll_ddr_other_clk,
    CLK_PLL_DDR_OTHER,				/* id */
    "pll_ddr_other", pll_parents,		/* name, parents */
    0x20,					/* offset */
    8, 5, 0, AW_CLK_FACTOR_ZERO_BASED,		/* n factor */
    4, 2, 0, 0,					/* k factor */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* m factor (fake) */
    2, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* p factor */
    31,						/* gate */
    0, 0,					/* lock */
    AW_CLK_HAS_GATE);				/* flags */
NKMP_CLK(pll_ddr_clk,
    CLK_PLL_DDR,				/* id */
    "pll_ddr", pll_parents,			/* name, parents */
    0x20,					/* offset */
    8, 5, 0, AW_CLK_FACTOR_ZERO_BASED,		/* n factor */
    4, 2, 0, 0,					/* k factor */
    0, 2, 0, 0,					/* m factor */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* p factor (fake) */
    31,						/* gate */
    0, 0,					/* lock */
    AW_CLK_HAS_GATE);				/* flags */

NKMP_CLK(pll6_clk,
    CLK_PLL6,					/* id */
    "pll6", pll_parents,			/* name, parents */
    0x28,					/* offset */
    8, 5, 0, AW_CLK_FACTOR_ZERO_BASED,		/* n factor */
    4, 2, 0, 0,					/* k factor */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* m factor (fake) */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* p factor (fake) */
    31,						/* gate */
    0, 0,					/* lock */
    AW_CLK_HAS_GATE);				/* flags */

static const char *pll6_parents[] = {"pll6"};
FIXED_CLK(pll_periph_clk,
    CLK_PLL_PERIPH,				/* id */
    "pll_periph", pll6_parents,			/* name, parents */
    0,						/* freq */
    1,						/* mult */
    2,						/* div */
    0);						/* flags */
NKMP_CLK(pll_periph_sata_clk,
    CLK_PLL_SATA,				/* id */
    "pll_periph_sata", pll6_parents,		/* name, parents */
    0x28,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* k factor (fake) */
    0, 2, 0, 0,					/* m factor */
    0, 0, 6, AW_CLK_FACTOR_FIXED,		/* p factor (fake, 6) */
    14,						/* gate */
    0, 0,					/* lock */
    AW_CLK_HAS_GATE);				/* flags */

static const char *mod_parents[] = {"osc24M", "pll_periph", "pll_ddr_other"};
NM_CLK(nand_clk,
    CLK_NAND,					/* id */
    "nand", mod_parents,			/* name, parents */
    0x80,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

NM_CLK(ms_clk,
    CLK_MS,					/* id */
    "ms", mod_parents,				/* name, parents */
    0x84,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

NM_CLK(mmc0_clk,
    CLK_MMC0,					/* id */
    "mmc0", mod_parents,			/* name, parents */
    0x88,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE |
    AW_CLK_REPARENT);				/* flags */

NM_CLK(mmc1_clk,
    CLK_MMC1,					/* id */
    "mmc1", mod_parents,			/* name, parents */
    0x8c,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE |
    AW_CLK_REPARENT);				/* flags */

NM_CLK(mmc2_clk,
    CLK_MMC2,					/* id */
    "mmc2", mod_parents,			/* name, parents */
    0x90,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE |
    AW_CLK_REPARENT);				/* flags */

NM_CLK(mmc3_clk,
    CLK_MMC3,					/* id */
    "mmc3", mod_parents,			/* name, parents */
    0x94,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE |
    AW_CLK_REPARENT);				/* flags */

NM_CLK(ts_clk,
    CLK_TS,					/* id */
    "ts", mod_parents,				/* name, parents */
    0x94,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

NM_CLK(ss_clk,
    CLK_SS,					/* id */
    "ss", mod_parents,				/* name, parents */
    0x9c,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

NM_CLK(spi0_clk,
    CLK_SPI0,					/* id */
    "spi0", mod_parents,			/* name, parents */
    0xa0,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

NM_CLK(spi1_clk,
    CLK_SPI1,					/* id */
    "spi1", mod_parents,			/* name, parents */
    0xa4,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

NM_CLK(spi2_clk,
    CLK_SPI2,					/* id */
    "spi2", mod_parents,			/* name, parents */
    0xa8,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

/* MISSING CLK_PATA */

NM_CLK(ir0_clk,
    CLK_IR0,					/* id */
    "ir0", mod_parents,				/* name, parents */
    0xb0,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

NM_CLK(ir1_clk,
    CLK_IR1,					/* id */
    "ir1", mod_parents,				/* name, parents */
    0xb4,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

/* MISSING CLK_I2S0, CLK_AC97, CLK_SPDIF */

static const char *keypad_parents[] = {"osc24M", "osc24M", "osc32k"};
NM_CLK(keypad_clk,
    CLK_KEYPAD,					/* id */
    "keypad", keypad_parents,			/* name, parents */
    0xc4,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 5, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

static const char *sata_parents[] = {"pll_periph_sata", "osc32k"};
NM_CLK(sata_clk,
    CLK_SATA,					/* id */
    "sata", sata_parents,			/* name, parents */
    0xc8,					/* offset */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* n factor (fake) */
    0, 0, 1, AW_CLK_FACTOR_FIXED,		/* m factor (fake) */
    24, 1,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

NM_CLK(spi3_clk,
    CLK_SPI3,					/* id */
    "spi3", mod_parents,				/* name, parents */
    0xd4,					/* offset */
    16, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,	/* n factor */
    0, 4, 0, 0,					/* m factor */
    24, 2,					/* mux */
    31,						/* gate */
    AW_CLK_HAS_MUX | AW_CLK_HAS_GATE);		/* flags */

/* MISSING CLK_I2S1, CLK_I2S2, DE Clocks */

static struct aw_ccung_clk a10_ccu_clks[] = {
	{ .type = AW_CLK_NKMP, .clk.nkmp = &pll_core_clk},
	{ .type = AW_CLK_NKMP, .clk.nkmp = &pll_ddr_other_clk},
	{ .type = AW_CLK_NKMP, .clk.nkmp = &pll_ddr_clk},
	{ .type = AW_CLK_NKMP, .clk.nkmp = &pll6_clk},
	{ .type = AW_CLK_NKMP, .clk.nkmp = &pll_periph_sata_clk},
	{ .type = AW_CLK_NM, .clk.nm = &axi_clk},
	{ .type = AW_CLK_NM, .clk.nm = &ahb_clk},
	{ .type = AW_CLK_NM, .clk.nm = &apb0_clk},
	{ .type = AW_CLK_NM, .clk.nm = &apb1_clk},
	{ .type = AW_CLK_NM, .clk.nm = &pll_video0_clk},
	{ .type = AW_CLK_NM, .clk.nm = &pll_video1_clk},
	{ .type = AW_CLK_NM, .clk.nm = &nand_clk},
	{ .type = AW_CLK_NM, .clk.nm = &ms_clk},
	{ .type = AW_CLK_NM, .clk.nm = &mmc0_clk},
	{ .type = AW_CLK_NM, .clk.nm = &mmc1_clk},
	{ .type = AW_CLK_NM, .clk.nm = &mmc2_clk},
	{ .type = AW_CLK_NM, .clk.nm = &mmc3_clk},
	{ .type = AW_CLK_NM, .clk.nm = &ts_clk},
	{ .type = AW_CLK_NM, .clk.nm = &ss_clk},
	{ .type = AW_CLK_NM, .clk.nm = &spi0_clk},
	{ .type = AW_CLK_NM, .clk.nm = &spi1_clk},
	{ .type = AW_CLK_NM, .clk.nm = &spi2_clk},
	{ .type = AW_CLK_NM, .clk.nm = &ir0_clk},
	{ .type = AW_CLK_NM, .clk.nm = &ir1_clk},
	{ .type = AW_CLK_NM, .clk.nm = &keypad_clk},
	{ .type = AW_CLK_NM, .clk.nm = &sata_clk},
	{ .type = AW_CLK_NM, .clk.nm = &spi3_clk},
	{ .type = AW_CLK_MUX, .clk.mux = &cpu_clk},
	{ .type = AW_CLK_FIXED, .clk.fixed = &pll_periph_clk},
	{ .type = AW_CLK_FIXED, .clk.fixed = &pll_video0_2x_clk},
	{ .type = AW_CLK_FIXED, .clk.fixed = &pll_video1_2x_clk},
};

static struct aw_clk_init a10_init_clks[] = {
};

static struct ofw_compat_data compat_data[] = {
#if defined(SOC_ALLWINNER_A10)
	{ "allwinner,sun4i-a10-ccu", 1 },
#endif
#if defined(SOC_ALLWINNER_A20)
	{ "allwinner,sun7i-a20-ccu", 1 },
#endif
	{ NULL, 0},
};

static int
ccu_a10_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Allwinner A10/A20 Clock Control Unit NG");
	return (BUS_PROBE_DEFAULT);
}

static int
ccu_a10_attach(device_t dev)
{
	struct aw_ccung_softc *sc;

	sc = device_get_softc(dev);

	sc->resets = a10_ccu_resets;
	sc->nresets = nitems(a10_ccu_resets);
	sc->gates = a10_ccu_gates;
	sc->ngates = nitems(a10_ccu_gates);
	sc->clks = a10_ccu_clks;
	sc->nclks = nitems(a10_ccu_clks);
	sc->clk_init = a10_init_clks;
	sc->n_clk_init = nitems(a10_init_clks);

	return (aw_ccung_attach(dev));
}

static device_method_t ccu_a10ng_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ccu_a10_probe),
	DEVMETHOD(device_attach,	ccu_a10_attach),

	DEVMETHOD_END
};

static devclass_t ccu_a10ng_devclass;

DEFINE_CLASS_1(ccu_a10ng, ccu_a10ng_driver, ccu_a10ng_methods,
  sizeof(struct aw_ccung_softc), aw_ccung_driver);

EARLY_DRIVER_MODULE(ccu_a10ng, simplebus, ccu_a10ng_driver,
    ccu_a10ng_devclass, 0, 0, BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);
