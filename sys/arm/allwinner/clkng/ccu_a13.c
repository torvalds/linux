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

#include <dt-bindings/clock/sun5i-ccu.h>
#include <dt-bindings/reset/sun5i-ccu.h>

/* Non-exported clocks */

#define	CLK_PLL_CORE		2
#define	CLK_PLL_AUDIO_BASE	3
#define	CLK_PLL_AUDIO		4
#define	CLK_PLL_AUDIO_2X	5
#define	CLK_PLL_AUDIO_4X	6
#define	CLK_PLL_AUDIO_8X	7
#define	CLK_PLL_VIDEO0		8

#define	CLK_PLL_VE		10
#define	CLK_PLL_DDR_BASE	11
#define	CLK_PLL_DDR		12
#define	CLK_PLL_DDR_OTHER	13
#define	CLK_PLL_PERIPH		14
#define	CLK_PLL_VIDEO1		15

#define	CLK_AXI			18
#define	CLK_AHB			19
#define	CLK_APB0		20
#define	CLK_APB1		21
#define	CLK_DRAM_AXI		22

#define	CLK_TCON_CH1_SCLK	91

#define	CLK_MBUS		99

static struct aw_ccung_reset a13_ccu_resets[] = {
	CCU_RESET(RST_USB_PHY0, 0xcc, 0)
	CCU_RESET(RST_USB_PHY1, 0xcc, 1)

	CCU_RESET(RST_GPS, 0xd0, 30)

	CCU_RESET(RST_DE_BE, 0x104, 30)

	CCU_RESET(RST_DE_FE, 0x10c, 30)

	CCU_RESET(RST_TVE, 0x118, 29)
	CCU_RESET(RST_LCD, 0x118, 30)

	CCU_RESET(RST_CSI, 0x134, 30)

	CCU_RESET(RST_VE, 0x13c, 0)
	CCU_RESET(RST_GPU, 0x154, 30)
	CCU_RESET(RST_IEP, 0x160, 30)

};

static struct aw_ccung_gate a13_ccu_gates[] = {
	CCU_GATE(CLK_HOSC, "hosc", "osc24M", 0x50, 0)

	CCU_GATE(CLK_DRAM_AXI, "axi-dram", "axi", 0x5c, 0)

	CCU_GATE(CLK_AHB_OTG, "ahb-otg", "ahb", 0x60, 0)
	CCU_GATE(CLK_AHB_EHCI, "ahb-ehci", "ahb", 0x60, 1)
	CCU_GATE(CLK_AHB_OHCI, "ahb-ohci", "ahb", 0x60, 2)
	CCU_GATE(CLK_AHB_SS, "ahb-ss", "ahb", 0x60, 5)
	CCU_GATE(CLK_AHB_DMA, "ahb-dma", "ahb", 0x60, 6)
	CCU_GATE(CLK_AHB_BIST, "ahb-bist", "ahb", 0x60, 7)
	CCU_GATE(CLK_AHB_MMC0, "ahb-mmc0", "ahb", 0x60, 8)
	CCU_GATE(CLK_AHB_MMC1, "ahb-mmc1", "ahb", 0x60, 9)
	CCU_GATE(CLK_AHB_MMC2, "ahb-mmc2", "ahb", 0x60, 10)
	CCU_GATE(CLK_AHB_NAND, "ahb-nand", "ahb", 0x60, 13)
	CCU_GATE(CLK_AHB_SDRAM, "ahb-sdram", "ahb", 0x60, 14)
	CCU_GATE(CLK_AHB_SPI0, "ahb-spi0", "ahb", 0x60, 20)
	CCU_GATE(CLK_AHB_SPI1, "ahb-spi1", "ahb", 0x60, 21)
	CCU_GATE(CLK_AHB_SPI2, "ahb-spi2", "ahb", 0x60, 22)
	CCU_GATE(CLK_AHB_GPS, "ahb-gps", "ahb", 0x60, 26)
	CCU_GATE(CLK_AHB_HSTIMER, "ahb-hstimer", "ahb", 0x60, 28)

	CCU_GATE(CLK_AHB_VE, "ahb-ve", "ahb", 0x64, 0)
	CCU_GATE(CLK_AHB_LCD, "ahb-lcd", "ahb", 0x64, 4)
	CCU_GATE(CLK_AHB_CSI, "ahb-csi", "ahb", 0x64, 8)
	CCU_GATE(CLK_AHB_DE_BE, "ahb-de-be", "ahb", 0x64, 12)
	CCU_GATE(CLK_AHB_DE_FE, "ahb-de-fe", "ahb", 0x64, 14)
	CCU_GATE(CLK_AHB_IEP, "ahb-iep", "ahb", 0x64, 19)
	CCU_GATE(CLK_AHB_GPU, "ahb-gpu", "ahb", 0x64, 20)

	CCU_GATE(CLK_APB0_CODEC, "apb0-codec", "apb0", 0x68, 0)
	CCU_GATE(CLK_APB0_PIO, "apb0-pio", "apb0", 0x68, 5)
	CCU_GATE(CLK_APB0_IR, "apb0-ir", "apb0", 0x68, 6)

	CCU_GATE(CLK_APB1_I2C0, "apb1-i2c0", "apb1", 0x6c, 0)
	CCU_GATE(CLK_APB1_I2C1, "apb1-i2c1", "apb1", 0x6c, 1)
	CCU_GATE(CLK_APB1_I2C2, "apb1-i2c2", "apb1", 0x6c, 2)
	CCU_GATE(CLK_APB1_UART1, "apb1-uart1", "apb1", 0x6c, 17)
	CCU_GATE(CLK_APB1_UART3, "apb1-uart3", "apb1", 0x6c, 19)

	CCU_GATE(CLK_DRAM_VE, "dram-ve", "pll-ddr", 0x100, 0)
	CCU_GATE(CLK_DRAM_CSI, "dram-csi", "pll-ddr", 0x100, 1)
	CCU_GATE(CLK_DRAM_DE_FE, "dram-de-fe", "pll-ddr", 0x100, 25)
	CCU_GATE(CLK_DRAM_DE_BE, "dram-de-be", "pll-ddr", 0x100, 26)
	CCU_GATE(CLK_DRAM_ACE, "dram-ace", "pll-ddr", 0x100, 29)
	CCU_GATE(CLK_DRAM_IEP, "dram-iep", "pll-ddr", 0x100, 31)

	CCU_GATE(CLK_CODEC, "codec", "pll-audio", 0x140, 31)

	CCU_GATE(CLK_AVS, "avs", "hosc", 0x144, 31)
};

static const char *pll_parents[] = {"hosc"};
static struct aw_clk_nkmp_def pll_core = {
	.clkdef = {
		.id = CLK_PLL_CORE,
		.name = "pll-core",
		.parent_names = pll_parents,
		.parent_cnt = nitems(pll_parents),
	},
	.offset = 0x00,
	.n = {.shift = 8, .width = 5},
	.k = {.shift = 4, .width = 2},
	.m = {.shift = 0, .width = 2},
	.p = {.shift = 16, .width = 2},
	.gate_shift = 31,
	.flags = AW_CLK_HAS_GATE,
};

/* 
 * We only implement pll-audio for now
 * For pll-audio-2/4/8 x we need a way to change the frequency
 * of the parent clocks
 */
static struct aw_clk_nkmp_def pll_audio = {
	.clkdef = {
		.id = CLK_PLL_AUDIO,
		.name = "pll-audio",
		.parent_names = pll_parents,
		.parent_cnt = nitems(pll_parents),
	},
	.offset = 0x08,
	.n = {.shift = 8, .width = 7},
	.k = {.value = 1, .flags = AW_CLK_FACTOR_FIXED},
	.m = {.shift = 0, .width = 5},
	.p = {.shift = 26, .width = 4},
	.gate_shift = 31,
	.flags = AW_CLK_HAS_GATE,
};

/* Missing PLL3-Video */
/* Missing PLL4-VE */

static struct aw_clk_nkmp_def pll_ddr_base = {
	.clkdef = {
		.id = CLK_PLL_DDR_BASE,
		.name = "pll-ddr-base",
		.parent_names = pll_parents,
		.parent_cnt = nitems(pll_parents),
	},
	.offset = 0x20,
	.n = {.shift = 8, .width = 5},
	.k = {.shift = 4, .width = 2},
	.m = {.value = 1, .flags = AW_CLK_FACTOR_FIXED},
	.p = {.value = 1, .flags = AW_CLK_FACTOR_FIXED},
	.gate_shift = 31,
	.flags = AW_CLK_HAS_GATE,
};

static const char *pll_ddr_parents[] = {"pll-ddr-base"};
static struct clk_div_def pll_ddr = {
	.clkdef = {
		.id = CLK_PLL_DDR,
		.name = "pll-ddr",
		.parent_names = pll_ddr_parents,
		.parent_cnt = nitems(pll_ddr_parents),
	},
	.offset = 0x20,
	.i_shift = 0,
	.i_width = 2,
};

static const char *pll_ddr_other_parents[] = {"pll-ddr-base"};
static struct clk_div_def pll_ddr_other = {
	.clkdef = {
		.id = CLK_PLL_DDR_OTHER,
		.name = "pll-ddr-other",
		.parent_names = pll_ddr_other_parents,
		.parent_cnt = nitems(pll_ddr_other_parents),
	},
	.offset = 0x20,
	.i_shift = 16,
	.i_width = 2,
};

static struct aw_clk_nkmp_def pll_periph = {
	.clkdef = {
		.id = CLK_PLL_PERIPH,
		.name = "pll-periph",
		.parent_names = pll_parents,
		.parent_cnt = nitems(pll_parents),
	},
	.offset = 0x28,
	.n = {.shift = 8, .width = 5},
	.k = {.shift = 4, .width = 2},
	.m = {.shift = 0, .width = 2},
	.p = {.value = 2, .flags = AW_CLK_FACTOR_FIXED},
	.gate_shift = 31,
	.flags = AW_CLK_HAS_GATE,
};

/* Missing PLL7-VIDEO1 */

static const char *cpu_parents[] = {"osc32k", "hosc", "pll-core", "pll-periph"};
static struct aw_clk_prediv_mux_def cpu_clk = {
	.clkdef = {
		.id = CLK_CPU,
		.name = "cpu",
		.parent_names = cpu_parents,
		.parent_cnt = nitems(cpu_parents),
	},
	.offset = 0x54,
	.mux_shift = 16, .mux_width = 2,
	.prediv = {
		.value = 6,
		.flags = AW_CLK_FACTOR_FIXED,
		.cond_shift = 16,
		.cond_width = 2,
		.cond_value = 3,
	},
};

static const char *axi_parents[] = {"cpu"};
static struct clk_div_def axi_clk = {
	.clkdef = {
		.id = CLK_AXI,
		.name = "axi",
		.parent_names = axi_parents,
		.parent_cnt = nitems(axi_parents),
	},
	.offset = 0x50,
	.i_shift = 0, .i_width = 2,
};

static const char *ahb_parents[] = {"axi", "cpu", "pll-periph"};
static struct aw_clk_prediv_mux_def ahb_clk = {
	.clkdef = {
		.id = CLK_AHB,
		.name = "ahb",
		.parent_names = ahb_parents,
		.parent_cnt = nitems(ahb_parents),
	},
	.offset = 0x54,
	.mux_shift = 6,
	.mux_width = 2,
	.div = {
		.shift = 4,
		.width = 2,
		.flags = AW_CLK_FACTOR_POWER_OF_TWO
	},
	.prediv = {
		.value = 2,
		.flags = AW_CLK_FACTOR_FIXED,
		.cond_shift = 6,
		.cond_width = 2,
		.cond_value = 2,
	},
};

static const char *apb0_parents[] = {"ahb"};
static struct clk_div_table apb0_div_table[] = {
	{ .value = 0, .divider = 2, },
	{ .value = 1, .divider = 2, },
	{ .value = 2, .divider = 4, },
	{ .value = 3, .divider = 8, },
	{ },
};
static struct clk_div_def apb0_clk = {
	.clkdef = {
		.id = CLK_APB0,
		.name = "apb0",
		.parent_names = apb0_parents,
		.parent_cnt = nitems(apb0_parents),
	},
	.offset = 0x54,
	.i_shift = 8, .i_width = 2,
	.div_flags = CLK_DIV_WITH_TABLE,
	.div_table = apb0_div_table,
};

static const char *apb1_parents[] = {"hosc", "pll-periph", "osc32k"};
static struct aw_clk_nm_def apb1_clk = {
	.clkdef = {
		.id = CLK_APB1,
		.name = "apb1",
		.parent_names = apb1_parents,
		.parent_cnt = nitems(apb1_parents),
	},
	.offset = 0x58,
	.n = {.shift = 16, .width = 2, .flags = AW_CLK_FACTOR_POWER_OF_TWO, },
	.m = {.shift = 0, .width = 5},
	.mux_shift = 24,
	.mux_width = 2,
	.flags = AW_CLK_HAS_MUX,
};

static const char *mod_parents[] = {"hosc", "pll-periph", "pll-ddr-other"};

static struct aw_clk_nm_def nand_clk = {
	.clkdef = {
		.id = CLK_NAND,
		.name = "nand",
		.parent_names = mod_parents,
		.parent_cnt = nitems(mod_parents),
	},
	.offset = 0x80,
	.n = {.shift = 16, .width = 2, .flags = AW_CLK_FACTOR_POWER_OF_TWO, },
	.m = {.shift = 0, .width = 4},
	.mux_shift = 24,
	.mux_width = 2,
	.gate_shift = 31,
	.flags = AW_CLK_HAS_MUX | AW_CLK_HAS_GATE | AW_CLK_REPARENT
};

static struct aw_clk_nm_def mmc0_clk = {
	.clkdef = {
		.id = CLK_MMC0,
		.name = "mmc0",
		.parent_names = mod_parents,
		.parent_cnt = nitems(mod_parents),
	},
	.offset = 0x88,
	.n = {.shift = 16, .width = 2, .flags = AW_CLK_FACTOR_POWER_OF_TWO, },
	.m = {.shift = 0, .width = 4},
	.mux_shift = 24,
	.mux_width = 2,
	.gate_shift = 31,
	.flags = AW_CLK_HAS_MUX | AW_CLK_HAS_GATE | AW_CLK_REPARENT
};

static struct aw_clk_nm_def mmc1_clk = {
	.clkdef = {
		.id = CLK_MMC1,
		.name = "mmc1",
		.parent_names = mod_parents,
		.parent_cnt = nitems(mod_parents),
	},
	.offset = 0x8C,
	.n = {.shift = 16, .width = 2, .flags = AW_CLK_FACTOR_POWER_OF_TWO, },
	.m = {.shift = 0, .width = 4},
	.mux_shift = 24,
	.mux_width = 2,
	.gate_shift = 31,
	.flags = AW_CLK_HAS_MUX | AW_CLK_HAS_GATE | AW_CLK_REPARENT
};

static struct aw_clk_nm_def mmc2_clk = {
	.clkdef = {
		.id = CLK_MMC2,
		.name = "mmc2",
		.parent_names = mod_parents,
		.parent_cnt = nitems(mod_parents),
	},
	.offset = 0x90,
	.n = {.shift = 16, .width = 2, .flags = AW_CLK_FACTOR_POWER_OF_TWO, },
	.m = {.shift = 0, .width = 4},
	.mux_shift = 24,
	.mux_width = 2,
	.gate_shift = 31,
	.flags = AW_CLK_HAS_MUX | AW_CLK_HAS_GATE | AW_CLK_REPARENT
};

static struct aw_clk_nm_def ss_clk = {
	.clkdef = {
		.id = CLK_SS,
		.name = "ss",
		.parent_names = mod_parents,
		.parent_cnt = nitems(mod_parents),
	},
	.offset = 0x9C,
	.n = {.shift = 16, .width = 2, .flags = AW_CLK_FACTOR_POWER_OF_TWO, },
	.m = {.shift = 0, .width = 4},
	.mux_shift = 24,
	.mux_width = 2,
	.gate_shift = 31,
	.flags = AW_CLK_HAS_MUX | AW_CLK_HAS_GATE | AW_CLK_REPARENT
};

static struct aw_clk_nm_def spi0_clk = {
	.clkdef = {
		.id = CLK_SPI0,
		.name = "spi0",
		.parent_names = mod_parents,
		.parent_cnt = nitems(mod_parents),
	},
	.offset = 0xA0,
	.n = {.shift = 16, .width = 2, .flags = AW_CLK_FACTOR_POWER_OF_TWO, },
	.m = {.shift = 0, .width = 4},
	.mux_shift = 24,
	.mux_width = 2,
	.gate_shift = 31,
	.flags = AW_CLK_HAS_MUX | AW_CLK_HAS_GATE | AW_CLK_REPARENT
};

static struct aw_clk_nm_def spi1_clk = {
	.clkdef = {
		.id = CLK_SPI1,
		.name = "spi1",
		.parent_names = mod_parents,
		.parent_cnt = nitems(mod_parents),
	},
	.offset = 0xA4,
	.n = {.shift = 16, .width = 2, .flags = AW_CLK_FACTOR_POWER_OF_TWO, },
	.m = {.shift = 0, .width = 4},
	.mux_shift = 24,
	.mux_width = 2,
	.gate_shift = 31,
	.flags = AW_CLK_HAS_MUX | AW_CLK_HAS_GATE | AW_CLK_REPARENT
};

static struct aw_clk_nm_def spi2_clk = {
	.clkdef = {
		.id = CLK_SPI2,
		.name = "spi2",
		.parent_names = mod_parents,
		.parent_cnt = nitems(mod_parents),
	},
	.offset = 0xA8,
	.n = {.shift = 16, .width = 2, .flags = AW_CLK_FACTOR_POWER_OF_TWO, },
	.m = {.shift = 0, .width = 4},
	.mux_shift = 24,
	.mux_width = 2,
	.gate_shift = 31,
	.flags = AW_CLK_HAS_MUX | AW_CLK_HAS_GATE | AW_CLK_REPARENT
};

static struct aw_clk_nm_def ir_clk = {
	.clkdef = {
		.id = CLK_IR,
		.name = "ir",
		.parent_names = mod_parents,
		.parent_cnt = nitems(mod_parents),
	},
	.offset = 0xB0,
	.n = {.shift = 16, .width = 2, .flags = AW_CLK_FACTOR_POWER_OF_TWO, },
	.m = {.shift = 0, .width = 4},
	.mux_shift = 24,
	.mux_width = 2,
	.gate_shift = 31,
	.flags = AW_CLK_HAS_MUX | AW_CLK_HAS_GATE | AW_CLK_REPARENT
};

/* Missing DE-BE clock */
/* Missing DE-FE clock */
/* Missing LCD CH1 clock */
/* Missing CSI clock */
/* Missing VE clock */


/* Clocks list */
static struct aw_ccung_clk a13_ccu_clks[] = {
	{ .type = AW_CLK_NKMP, .clk.nkmp = &pll_core},
	{ .type = AW_CLK_NKMP, .clk.nkmp = &pll_audio},
	{ .type = AW_CLK_NKMP, .clk.nkmp = &pll_ddr_base},
	{ .type = AW_CLK_NKMP, .clk.nkmp = &pll_periph},
	{ .type = AW_CLK_NM, .clk.nm = &apb1_clk},
	{ .type = AW_CLK_NM, .clk.nm = &nand_clk},
	{ .type = AW_CLK_NM, .clk.nm = &mmc0_clk},
	{ .type = AW_CLK_NM, .clk.nm = &mmc1_clk},
	{ .type = AW_CLK_NM, .clk.nm = &mmc2_clk},
	{ .type = AW_CLK_NM, .clk.nm = &ss_clk},
	{ .type = AW_CLK_NM, .clk.nm = &spi0_clk},
	{ .type = AW_CLK_NM, .clk.nm = &spi1_clk},
	{ .type = AW_CLK_NM, .clk.nm = &spi2_clk},
	{ .type = AW_CLK_NM, .clk.nm = &ir_clk},
	{ .type = AW_CLK_PREDIV_MUX, .clk.prediv_mux = &cpu_clk},
	{ .type = AW_CLK_PREDIV_MUX, .clk.prediv_mux = &ahb_clk},
	{ .type = AW_CLK_DIV, .clk.div = &pll_ddr},
	{ .type = AW_CLK_DIV, .clk.div = &pll_ddr_other},
	{ .type = AW_CLK_DIV, .clk.div = &axi_clk},
	{ .type = AW_CLK_DIV, .clk.div = &apb0_clk},
};

static int
ccu_a13_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "allwinner,sun5i-a13-ccu"))
		return (ENXIO);

	device_set_desc(dev, "Allwinner A13 Clock Control Unit NG");
	return (BUS_PROBE_DEFAULT);
}

static int
ccu_a13_attach(device_t dev)
{
	struct aw_ccung_softc *sc;

	sc = device_get_softc(dev);

	sc->resets = a13_ccu_resets;
	sc->nresets = nitems(a13_ccu_resets);
	sc->gates = a13_ccu_gates;
	sc->ngates = nitems(a13_ccu_gates);
	sc->clks = a13_ccu_clks;
	sc->nclks = nitems(a13_ccu_clks);

	return (aw_ccung_attach(dev));
}

static device_method_t ccu_a13ng_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ccu_a13_probe),
	DEVMETHOD(device_attach,	ccu_a13_attach),

	DEVMETHOD_END
};

static devclass_t ccu_a13ng_devclass;

DEFINE_CLASS_1(ccu_a13ng, ccu_a13ng_driver, ccu_a13ng_methods,
  sizeof(struct aw_ccung_softc), aw_ccung_driver);

EARLY_DRIVER_MODULE(ccu_a13ng, simplebus, ccu_a13ng_driver,
    ccu_a13ng_devclass, 0, 0, BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);
