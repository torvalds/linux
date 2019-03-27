/*-
 * Copyright 2015 Alexander Kabaev <kan@FreeBSD.org>
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

/*
 * Ingenic JZ4780 CGU driver.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/rman.h>

#include <machine/bus.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <mips/ingenic/jz4780_clk.h>
#include <mips/ingenic/jz4780_regs.h>
#include <mips/ingenic/jz4780_clock.h>

#include "clkdev_if.h"

#include <gnu/dts/include/dt-bindings/clock/jz4780-cgu.h>

/**********************************************************************
 *  JZ4780 CGU clock domain
 **********************************************************************/
struct jz4780_clock_softc {
	device_t	dev;
	struct resource	*res[1];
	struct mtx	mtx;
	struct clkdom	*clkdom;
};

static struct resource_spec jz4780_clock_spec[] = {
	{ SYS_RES_MEMORY, 0, RF_ACTIVE },
	{ -1, 0 }
};

struct jz4780_clk_pll_def {
	uint16_t   clk_id;
	uint16_t   clk_reg;
	const char *clk_name;
	const char *clk_pname[1];
};

#define PLL(_id, cname, pname, reg) {		\
	.clk_id		= _id,			\
	.clk_reg	= reg,			\
	.clk_name	= cname,		\
	.clk_pname[0]	= pname,		\
}

struct jz4780_clk_gate_def {
	uint16_t   clk_id;
	uint16_t   clk_bit;
	const char *clk_name;
	const char *clk_pname[1];
};

#define GATE(_id, cname, pname, bit) {		\
	.clk_id		= _id,			\
	.clk_bit	= bit,			\
	.clk_name	= cname,		\
	.clk_pname[0]	= pname,		\
}

#define MUX(reg, shift, bits, map)			\
    .clk_mux.mux_reg = (reg),				\
    .clk_mux.mux_shift = (shift),			\
    .clk_mux.mux_bits = (bits),				\
    .clk_mux.mux_map = (map),
#define NO_MUX

#define DIV(reg, shift, lg, bits, ce, st, bb)		\
    .clk_div.div_reg = (reg),				\
    .clk_div.div_shift = (shift),			\
    .clk_div.div_bits = (bits),				\
    .clk_div.div_lg = (lg),				\
    .clk_div.div_ce_bit = (ce),				\
    .clk_div.div_st_bit = (st),				\
    .clk_div.div_busy_bit = (bb),
#define NO_DIV						\

#define GATEBIT(bit)					\
    .clk_gate_bit = (bit),
#define NO_GATE						\
    .clk_gate_bit = (-1),

#define PLIST(pnames...)				\
    .clk_pnames = { pnames },

#define GENCLK(id, name, type, parents, mux, div, gt) {	\
	.clk_id		= id,				\
	.clk_type       = type,				\
	.clk_name	= name,				\
	parents						\
	mux						\
	div						\
	gt						\
}

/* PLL definitions */
static struct jz4780_clk_pll_def pll_clks[] = {
	PLL(JZ4780_CLK_APLL, "apll", "ext", JZ_CPAPCR),
	PLL(JZ4780_CLK_MPLL, "mpll", "ext", JZ_CPMPCR),
	PLL(JZ4780_CLK_EPLL, "epll", "ext", JZ_CPEPCR),
	PLL(JZ4780_CLK_VPLL, "vpll", "ext", JZ_CPVPCR),
};

/* OTG PHY clock (reuse gate def structure */
static struct jz4780_clk_gate_def otg_clks[] = {
	GATE(JZ4780_CLK_OTGPHY,	"otg_phy",	"ext", 0),
};

static const struct jz4780_clk_descr gen_clks[] = {
	GENCLK(JZ4780_CLK_SCLKA, "sclk_a", CLK_MASK_MUX,
	    PLIST("apll", "ext", "rtc"),
	    MUX(JZ_CPCCR, 30, 2, 0x7),
	    NO_DIV,
	    NO_GATE
	),

	GENCLK(JZ4780_CLK_CPUMUX, "cpumux", CLK_MASK_MUX,
	    PLIST("sclk_a", "mpll", "epll"),
	    MUX(JZ_CPCCR, 28, 2, 0x7),
	    NO_DIV,
	    NO_GATE
	),

	GENCLK(JZ4780_CLK_CPU, "cpu", CLK_MASK_DIV,
	    PLIST("cpumux"),
	    NO_MUX,
	    DIV(JZ_CPCCR, 0, 0, 4, 22, -1, -1),
	    NO_GATE
	),

	GENCLK(JZ4780_CLK_L2CACHE, "l2cache", CLK_MASK_DIV,
	    PLIST("cpumux"),
	    NO_MUX,
	    DIV(JZ_CPCCR, 4, 0, 4, -1, -1, -1),
	    NO_GATE
	),

	GENCLK(JZ4780_CLK_AHB0, "ahb0", CLK_MASK_MUX | CLK_MASK_DIV,
	    PLIST("sclk_a", "mpll", "epll"),
	    MUX(JZ_CPCCR, 26, 2, 0x7),
	    DIV(JZ_CPCCR, 8, 0, 4, 21, -1, -1),
	    NO_GATE
	),

	GENCLK(JZ4780_CLK_AHB2PMUX, "ahb2_apb_mux", CLK_MASK_MUX,
	    PLIST("sclk_a", "mpll", "rtc"),
	    MUX(JZ_CPCCR, 24, 2, 0x7),
	    NO_DIV,
	    NO_GATE
	),

	GENCLK(JZ4780_CLK_AHB2, "ahb2", CLK_MASK_DIV,
	    PLIST("ahb2_apb_mux"),
	    NO_MUX,
	    DIV(JZ_CPCCR, 12, 0, 4, 20, -1, -1),
	    NO_GATE
	),

	GENCLK(JZ4780_CLK_PCLK, "pclk", CLK_MASK_DIV,
	    PLIST("ahb2_apb_mux"),
	    NO_MUX,
	    DIV(JZ_CPCCR, 16, 0, 4, 20, -1, -1),
	    NO_GATE
	),

	GENCLK(JZ4780_CLK_DDR, "ddr", CLK_MASK_MUX | CLK_MASK_DIV,
	    PLIST("sclk_a", "mpll"),
	    MUX(JZ_DDCDR, 30, 2, 0x6),
	    DIV(JZ_DDCDR, 0, 0, 4, 29, 28, 27),
	    NO_GATE
	),

	GENCLK(JZ4780_CLK_VPU, "vpu", CLK_MASK_MUX | CLK_MASK_DIV | CLK_MASK_GATE,
	    PLIST("sclk_a", "mpll", "epll"),
	    MUX(JZ_VPUCDR, 30, 2, 0xe),
	    DIV(JZ_VPUCDR, 0, 0, 4, 29, 28, 27),
	    GATEBIT(32 + 2)
	),

	GENCLK(JZ4780_CLK_I2SPLL, "i2s_pll", CLK_MASK_MUX | CLK_MASK_DIV,
	    PLIST("sclk_a", "epll"),
	    MUX(JZ_I2SCDR, 30, 1, 0xc),
	    DIV(JZ_I2SCDR, 0, 0, 8, 29, 28, 27),
	    NO_GATE
	),

	GENCLK(JZ4780_CLK_I2S, "i2s", CLK_MASK_MUX,
	    PLIST("ext", "i2s_pll"),
	    MUX(JZ_I2SCDR, 31, 1, 0xc),
	    NO_DIV,
	    NO_GATE
	),

	GENCLK(JZ4780_CLK_LCD0PIXCLK, "lcd0pixclk", CLK_MASK_MUX | CLK_MASK_DIV,
	    PLIST("sclk_a", "mpll", "vpll"),
	    MUX(JZ_LP0CDR, 30, 2, 0xe),
	    DIV(JZ_LP0CDR, 0, 0, 8, 28, 27, 26),
	    NO_GATE
	),

	GENCLK(JZ4780_CLK_LCD1PIXCLK, "lcd1pixclk", CLK_MASK_MUX | CLK_MASK_DIV,
	    PLIST("sclk_a", "mpll", "vpll"),
	    MUX(JZ_LP1CDR, 30, 2, 0xe),
	    DIV(JZ_LP1CDR, 0, 0, 8, 28, 27, 26),
	    NO_GATE
	),

	GENCLK(JZ4780_CLK_MSCMUX, "msc_mux", CLK_MASK_MUX,
	    PLIST("sclk_a", "mpll"),
	    MUX(JZ_MSC0CDR, 30, 2, 0x6),
	    NO_DIV,
	    NO_GATE
	),

	GENCLK(JZ4780_CLK_MSC0, "msc0", CLK_MASK_DIV | CLK_MASK_GATE,
	    PLIST("msc_mux"),
	    NO_MUX,
	    DIV(JZ_MSC0CDR, 0, 1, 8, 29, 28, 27),
	    GATEBIT(3)
	),

	GENCLK(JZ4780_CLK_MSC1, "msc1", CLK_MASK_DIV | CLK_MASK_GATE,
	    PLIST("msc_mux"),
	    NO_MUX,
	    DIV(JZ_MSC1CDR, 0, 1, 8, 29, 28, 27),
	    GATEBIT(11)
	),

	GENCLK(JZ4780_CLK_MSC2, "msc2", CLK_MASK_DIV | CLK_MASK_GATE,
	    PLIST("msc_mux"),
	    NO_MUX,
	    DIV(JZ_MSC2CDR, 0, 1, 8, 29, 28, 27),
	    GATEBIT(12)
	),

	GENCLK(JZ4780_CLK_UHC, "uhc", CLK_MASK_MUX | CLK_MASK_DIV | CLK_MASK_GATE,
	    PLIST("sclk_a", "mpll", "epll", "otg_phy"),
	    MUX(JZ_UHCCDR, 30, 2, 0xf),
	    DIV(JZ_UHCCDR, 0, 0, 8, 29, 28, 27),
	    GATEBIT(24)
	),

	GENCLK(JZ4780_CLK_SSIPLL, "ssi_pll", CLK_MASK_MUX | CLK_MASK_DIV,
	    PLIST("sclk_a", "mpll"),
	    MUX(JZ_SSICDR, 30, 1, 0xc),
	    DIV(JZ_SSICDR, 0, 0, 8, 29, 28, 27),
	    NO_GATE
	),

	GENCLK(JZ4780_CLK_SSI, "ssi", CLK_MASK_MUX,
	    PLIST("ext", "ssi_pll"),
	    MUX(JZ_SSICDR, 31, 1, 0xc),
	    NO_DIV,
	    NO_GATE
	),

	GENCLK(JZ4780_CLK_CIMMCLK, "cim_mclk", CLK_MASK_MUX | CLK_MASK_DIV,
	    PLIST("sclk_a", "mpll"),
	    MUX(JZ_CIMCDR, 31, 1, 0xc),
	    DIV(JZ_CIMCDR, 0, 0, 8, 30, 29, 28),
	    NO_GATE
	),

	GENCLK(JZ4780_CLK_PCMPLL, "pcm_pll", CLK_MASK_MUX | CLK_MASK_DIV,
	    PLIST("sclk_a", "mpll", "epll", "vpll"),
	    MUX(JZ_PCMCDR, 29, 2, 0xf),
	    DIV(JZ_PCMCDR, 0, 0, 8, 28, 27, 26),
	    NO_GATE
	),

	GENCLK(JZ4780_CLK_PCM, "pcm", CLK_MASK_MUX | CLK_MASK_GATE,
	    PLIST("ext", "pcm_pll"),
	    MUX(JZ_PCMCDR, 31, 1, 0xc),
	    NO_DIV,
	    GATEBIT(32 + 3)
	),

	GENCLK(JZ4780_CLK_GPU, "gpu", CLK_MASK_MUX | CLK_MASK_DIV | CLK_MASK_GATE,
	    PLIST("sclk_a", "mpll", "epll"),
	    MUX(JZ_GPUCDR, 30, 2, 0x7),
	    DIV(JZ_GPUCDR, 0, 0, 4, 29, 28, 27),
	    GATEBIT(32 + 4)
	),

	GENCLK(JZ4780_CLK_HDMI, "hdmi", CLK_MASK_MUX | CLK_MASK_DIV | CLK_MASK_GATE,
	    PLIST("sclk_a", "mpll", "vpll"),
	    MUX(JZ_HDMICDR, 30, 2, 0xe),
	    DIV(JZ_HDMICDR, 0, 0, 8, 29, 28, 26),
	    GATEBIT(32 + 9)
	),

	GENCLK(JZ4780_CLK_BCH, "bch", CLK_MASK_MUX | CLK_MASK_DIV | CLK_MASK_GATE,
	    PLIST("sclk_a", "mpll", "epll"),
	    MUX(JZ_BCHCDR, 30, 2, 0x7),
	    DIV(JZ_BCHCDR, 0, 0, 4, 29, 28, 27),
	    GATEBIT(1)
	),
};

static struct jz4780_clk_gate_def gate_clks[] = {
	GATE(JZ4780_CLK_NEMC,	"nemc",		"ahb2", 0),
	GATE(JZ4780_CLK_OTG0,	"otg0",		"ext",	2),
	GATE(JZ4780_CLK_SSI0,	"ssi0",		"ssi",	4),
	GATE(JZ4780_CLK_SMB0,	"smb0",		"pclk", 5),
	GATE(JZ4780_CLK_SMB1,	"smb1",		"pclk", 6),
	GATE(JZ4780_CLK_SCC,	"scc",		"ext",	7),
	GATE(JZ4780_CLK_AIC,	"aic",		"ext",	8),
	GATE(JZ4780_CLK_TSSI0,	"tssi0",	"ext",	9),
	GATE(JZ4780_CLK_OWI,	"owi",		"ext",	10),
	GATE(JZ4780_CLK_KBC,	"kbc",		"ext",	13),
	GATE(JZ4780_CLK_SADC,	"sadc",		"ext",	14),
	GATE(JZ4780_CLK_UART0,	"uart0",	"ext",	15),
	GATE(JZ4780_CLK_UART1,	"uart1",	"ext",	16),
	GATE(JZ4780_CLK_UART2,	"uart2",	"ext",	17),
	GATE(JZ4780_CLK_UART3,	"uart3",	"ext",	18),
	GATE(JZ4780_CLK_SSI1,	"ssi1",		"ssi",	19),
	GATE(JZ4780_CLK_SSI2,	"ssi2",		"ssi",	20),
	GATE(JZ4780_CLK_PDMA,	"pdma",		"ext",	21),
	GATE(JZ4780_CLK_GPS,	"gps",		"ext",	22),
	GATE(JZ4780_CLK_MAC,	"mac",		"ext",	23),
	GATE(JZ4780_CLK_SMB2,	"smb2",		"pclk",	25),
	GATE(JZ4780_CLK_CIM,	"cim",		"ext",	26),
	GATE(JZ4780_CLK_LCD,	"lcd",		"ext",	28),
	GATE(JZ4780_CLK_TVE,	"tve",		"lcd",	27),
	GATE(JZ4780_CLK_IPU,	"ipu",		"ext",	29),
	GATE(JZ4780_CLK_DDR0,	"ddr0",		"ddr",	30),
	GATE(JZ4780_CLK_DDR1,	"ddr1",		"ddr",	31),
	GATE(JZ4780_CLK_SMB3,	"smb3",		"pclk",	32 + 0),
	GATE(JZ4780_CLK_TSSI1,	"tssi1",	"ext",	32 + 1),
	GATE(JZ4780_CLK_COMPRESS, "compress",	"ext",	32 + 5),
	GATE(JZ4780_CLK_AIC1,	"aic1",		"ext",	32 + 6),
	GATE(JZ4780_CLK_GPVLC,	"gpvlc",	"ext",	32 + 7),
	GATE(JZ4780_CLK_OTG1,	"otg1",		"ext",	32 + 8),
	GATE(JZ4780_CLK_UART4,	"uart4",	"ext",	32 + 10),
	GATE(JZ4780_CLK_AHBMON,	"ahb_mon",	"ext",	32 + 11),
	GATE(JZ4780_CLK_SMB4,	"smb4",		"pclk", 32 + 12),
	GATE(JZ4780_CLK_DES,	"des",		"ext",	32 + 13),
	GATE(JZ4780_CLK_X2D,	"x2d",		"ext",	32 + 14),
	GATE(JZ4780_CLK_CORE1,	"core1",	"cpu",	32 + 15),
};

static int
jz4780_clock_register(struct jz4780_clock_softc *sc)
{
	int i, ret;

	/* Register PLLs */
	for (i = 0; i < nitems(pll_clks); i++) {
		struct clknode_init_def clkdef;

		clkdef.id = pll_clks[i].clk_id;
		clkdef.name = __DECONST(char *, pll_clks[i].clk_name);
		clkdef.parent_names = pll_clks[i].clk_pname;
		clkdef.parent_cnt = 1;
		clkdef.flags = CLK_NODE_STATIC_STRINGS;

		ret = jz4780_clk_pll_register(sc->clkdom, &clkdef, &sc->mtx,
		    sc->res[0], pll_clks[i].clk_reg);
		if (ret != 0)
			return (ret);
	}

	/* Register OTG clock */
	for (i = 0; i < nitems(otg_clks); i++) {
		struct clknode_init_def clkdef;

		clkdef.id = otg_clks[i].clk_id;
		clkdef.name = __DECONST(char *, otg_clks[i].clk_name);
		clkdef.parent_names = otg_clks[i].clk_pname;
		clkdef.parent_cnt = 1;
		clkdef.flags = CLK_NODE_STATIC_STRINGS;

		ret = jz4780_clk_otg_register(sc->clkdom, &clkdef, &sc->mtx,
		    sc->res[0]);
		if (ret != 0)
			return (ret);
	}

	/* Register muxes and divisors */
	for (i = 0; i < nitems(gen_clks); i++) {
		ret = jz4780_clk_gen_register(sc->clkdom, &gen_clks[i],
		    &sc->mtx, sc->res[0]);
		if (ret != 0)
			return (ret);
	}

	/* Register simple gates */
	for (i = 0; i < nitems(gate_clks); i++) {
		struct clk_gate_def gatedef;

		gatedef.clkdef.id = gate_clks[i].clk_id;
		gatedef.clkdef.name = __DECONST(char *, gate_clks[i].clk_name);
		gatedef.clkdef.parent_names = gate_clks[i].clk_pname;
		gatedef.clkdef.parent_cnt = 1;
		gatedef.clkdef.flags = CLK_NODE_STATIC_STRINGS;

		if (gate_clks[i].clk_bit < 32) {
			gatedef.offset = JZ_CLKGR0;
			gatedef.shift = gate_clks[i].clk_bit;
		} else {
			gatedef.offset = JZ_CLKGR1;
			gatedef.shift = gate_clks[i].clk_bit - 32;
		}
		gatedef.mask = 1;
		gatedef.on_value = 0;
		gatedef.off_value = 1;
		gatedef.gate_flags = 0;

		ret = clknode_gate_register(sc->clkdom, &gatedef);
		if (ret != 0)
			return (ret);

	}

	return (0);
}

static int
jz4780_clock_fixup(struct jz4780_clock_softc *sc)
{
	struct clknode *clk_uhc;
	int ret;

	/*
	 * Make UHC mux use MPLL as the source. It defaults to OTG_PHY
	 * and that somehow just does not work.
	 */
	clkdom_xlock(sc->clkdom);

	/* Assume the worst */
	ret = ENXIO;

	clk_uhc = clknode_find_by_id(sc->clkdom, JZ4780_CLK_UHC);
	if (clk_uhc != NULL) {
		ret = clknode_set_parent_by_name(clk_uhc, "mpll");
		if (ret != 0)
			device_printf(sc->dev,
			    "unable to reparent uhc clock\n");
		else
			ret = clknode_set_freq(clk_uhc, 48000000, 0, 0);
		if (ret != 0)
			device_printf(sc->dev, "unable to init uhc clock\n");
	} else
		device_printf(sc->dev, "unable to lookup uhc clock\n");

	clkdom_unlock(sc->clkdom);
	return (ret);
}

#define	CGU_LOCK(sc)		mtx_lock(&(sc)->mtx)
#define	CGU_UNLOCK(sc)		mtx_unlock(&(sc)->mtx)
#define	CGU_LOCK_INIT(sc)	\
    mtx_init(&(sc)->mtx, device_get_nameunit((sc)->dev),	\
    "jz4780-cgu", MTX_DEF)
#define	CGU_LOCK_DESTROY(sc)	mtx_destroy(&(sc)->mtx);

#define CSR_WRITE_4(sc, reg, val)	bus_write_4((sc)->res[0], (reg), (val))
#define CSR_READ_4(sc, reg)		bus_read_4((sc)->res[0], (reg))

static int
jz4780_clock_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "ingenic,jz4780-cgu"))
		return (ENXIO);

	device_set_desc(dev, "Ingenic jz4780 CGU");

	return (BUS_PROBE_DEFAULT);
}

static int
jz4780_clock_attach(device_t dev)
{
	struct jz4780_clock_softc *sc;

	sc = device_get_softc(dev);
	if (bus_alloc_resources(dev, jz4780_clock_spec, sc->res)) {
		device_printf(dev, "could not allocate resources for device\n");
		return (ENXIO);
	}

	sc->dev = dev;
	CGU_LOCK_INIT(sc);

	sc->clkdom = clkdom_create(dev);
	if (sc->clkdom == NULL)
		goto fail;
	if (jz4780_clock_register(sc) != 0)
		goto fail;
	if (clkdom_finit(sc->clkdom) != 0)
		goto fail;
	if (jz4780_clock_fixup(sc) != 0)
		goto fail;
	if (bootverbose)
		clkdom_dump(sc->clkdom);

	return (0);
fail:
	bus_release_resources(dev, jz4780_clock_spec, sc->res);
	CGU_LOCK_DESTROY(sc);

	return (ENXIO);
}

static int
jz4780_clock_detach(device_t dev)
{
	struct jz4780_clock_softc *sc;

	sc = device_get_softc(dev);
	bus_release_resources(dev, jz4780_clock_spec, sc->res);
	CGU_LOCK_DESTROY(sc);

	return (0);
}

static int
jz4780_clock_write_4(device_t dev, bus_addr_t addr, uint32_t val)
{
	struct jz4780_clock_softc *sc;

	sc = device_get_softc(dev);
	CSR_WRITE_4(sc, addr, val);
	return (0);
}

static int
jz4780_clock_read_4(device_t dev, bus_addr_t addr, uint32_t *val)
{
	struct jz4780_clock_softc *sc;

	sc = device_get_softc(dev);
	*val = CSR_READ_4(sc, addr);
	return (0);
}

static int
jz4780_clock_modify_4(device_t dev, bus_addr_t addr, uint32_t clear_mask,
    uint32_t set_mask)
{
	struct jz4780_clock_softc *sc;
	uint32_t val;

	sc = device_get_softc(dev);
	val = CSR_READ_4(sc, addr);
	val &= ~clear_mask;
	val |= set_mask;
	CSR_WRITE_4(sc, addr, val);
	return (0);
}

static void
jz4780_clock_device_lock(device_t dev)
{
	struct jz4780_clock_softc *sc;

	sc = device_get_softc(dev);
	CGU_LOCK(sc);
}

static void
jz4780_clock_device_unlock(device_t dev)
{
	struct jz4780_clock_softc *sc;

	sc = device_get_softc(dev);
	CGU_UNLOCK(sc);
}

static device_method_t jz4780_clock_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		jz4780_clock_probe),
	DEVMETHOD(device_attach,	jz4780_clock_attach),
	DEVMETHOD(device_detach,	jz4780_clock_detach),

	/* Clock device interface */
	DEVMETHOD(clkdev_write_4,	jz4780_clock_write_4),
	DEVMETHOD(clkdev_read_4,	jz4780_clock_read_4),
	DEVMETHOD(clkdev_modify_4,	jz4780_clock_modify_4),
	DEVMETHOD(clkdev_device_lock,	jz4780_clock_device_lock),
	DEVMETHOD(clkdev_device_unlock,	jz4780_clock_device_unlock),

	DEVMETHOD_END
};

static driver_t jz4780_clock_driver = {
	"cgu",
	jz4780_clock_methods,
	sizeof(struct jz4780_clock_softc),
};

static devclass_t jz4780_clock_devclass;

EARLY_DRIVER_MODULE(jz4780_clock, simplebus, jz4780_clock_driver,
    jz4780_clock_devclass, 0, 0,  BUS_PASS_CPU + BUS_PASS_ORDER_LATE);

static int
jz4780_ehci_clk_config(struct jz4780_clock_softc *sc)
{
	clk_t phy_clk, ext_clk;
	uint64_t phy_freq;
	int err;

	phy_clk = NULL;
	ext_clk = NULL;
	err = -1;

	/* Set phy timing by copying it from ext */
	if (clk_get_by_id(sc->dev, sc->clkdom, JZ4780_CLK_OTGPHY,
	    &phy_clk) != 0)
		goto done;
	if (clk_get_parent(phy_clk, &ext_clk) != 0)
		goto done;
	if (clk_get_freq(ext_clk, &phy_freq) != 0)
		goto done;
	if (clk_set_freq(phy_clk, phy_freq, 0) != 0)
		goto done;
	err = 0;
done:
	clk_release(ext_clk);
	clk_release(phy_clk);

	return (err);
}

int
jz4780_ohci_enable(void)
{
	device_t dev;
	struct jz4780_clock_softc *sc;
	uint32_t reg;

	dev = devclass_get_device(jz4780_clock_devclass, 0);
	if (dev == NULL)
		return (-1);

	sc = device_get_softc(dev);
	CGU_LOCK(sc);

	/* Do not force port1 to suspend mode */
	reg = CSR_READ_4(sc, JZ_OPCR);
	reg |= OPCR_SPENDN1;
	CSR_WRITE_4(sc, JZ_OPCR, reg);

	CGU_UNLOCK(sc);
	return (0);
}

int
jz4780_ehci_enable(void)
{
	device_t dev;
	struct jz4780_clock_softc *sc;
	uint32_t reg;

	dev = devclass_get_device(jz4780_clock_devclass, 0);
	if (dev == NULL)
		return (-1);

	sc = device_get_softc(dev);

	/*
	 * EHCI should use MPPL as a parent, but Linux configures OTG
	 * clock anyway. Follow their lead blindly.
	 */
	if (jz4780_ehci_clk_config(sc) != 0)
		return (-1);

	CGU_LOCK(sc);

	/* Enable OTG, should not be necessary since we use PLL clock */
	reg = CSR_READ_4(sc, JZ_USBPCR);
	reg &= ~(PCR_OTG_DISABLE);
	CSR_WRITE_4(sc, JZ_USBPCR, reg);

	/* Do not force port1 to suspend mode */
	reg = CSR_READ_4(sc, JZ_OPCR);
	reg |= OPCR_SPENDN1;
	CSR_WRITE_4(sc, JZ_OPCR, reg);

	/* D- pulldown */
	reg = CSR_READ_4(sc, JZ_USBPCR1);
	reg |= PCR_DMPD1;
	CSR_WRITE_4(sc, JZ_USBPCR1, reg);

	/* D+ pulldown */
	reg = CSR_READ_4(sc, JZ_USBPCR1);
	reg |= PCR_DPPD1;
	CSR_WRITE_4(sc, JZ_USBPCR1, reg);

	/* 16 bit bus witdth for port 1*/
	reg = CSR_READ_4(sc, JZ_USBPCR1);
	reg |= PCR_WORD_I_F1 | PCR_WORD_I_F0;
	CSR_WRITE_4(sc, JZ_USBPCR1, reg);

	/* Reset USB */
	reg = CSR_READ_4(sc, JZ_USBPCR);
	reg |= PCR_POR;
	CSR_WRITE_4(sc, JZ_USBPCR, reg);
	DELAY(1);
	reg = CSR_READ_4(sc, JZ_USBPCR);
	reg &= ~(PCR_POR);
	CSR_WRITE_4(sc, JZ_USBPCR, reg);

	/* Soft-reset USB */
	reg = CSR_READ_4(sc, JZ_SRBC);
	reg |= SRBC_UHC_SR;
	CSR_WRITE_4(sc, JZ_SRBC, reg);
	/* 300ms */
	DELAY(300*hz/1000);

	reg = CSR_READ_4(sc, JZ_SRBC);
	reg &= ~(SRBC_UHC_SR);
	CSR_WRITE_4(sc, JZ_SRBC, reg);

	/* 300ms */
	DELAY(300*hz/1000);

	CGU_UNLOCK(sc);
	return (0);
}

#define	USBRESET_DETECT_TIME	0x96

int
jz4780_otg_enable(void)
{
	device_t dev;
	struct jz4780_clock_softc *sc;
	uint32_t reg;

	dev = devclass_get_device(jz4780_clock_devclass, 0);
	if (dev == NULL)
		return (-1);

	sc = device_get_softc(dev);

	CGU_LOCK(sc);

	/* Select Synopsys OTG mode */
	reg = CSR_READ_4(sc, JZ_USBPCR1);
	reg |= PCR_SYNOPSYS;

	/* Set UTMI bus width to 16 bit */
	reg |= PCR_WORD_I_F0 | PCR_WORD_I_F1;
	CSR_WRITE_4(sc, JZ_USBPCR1, reg);

	/* Blah */
	reg = CSR_READ_4(sc, JZ_USBVBFIL);
	reg = REG_SET(reg, USBVBFIL_IDDIGFIL, 0);
	reg = REG_SET(reg, USBVBFIL_USBVBFIL, 0);
	CSR_WRITE_4(sc, JZ_USBVBFIL, reg);

	/* Setup reset detect time */
	reg = CSR_READ_4(sc, JZ_USBRDT);
	reg = REG_SET(reg, USBRDT_USBRDT, USBRESET_DETECT_TIME);
	reg |= USBRDT_VBFIL_LD_EN;
	CSR_WRITE_4(sc, JZ_USBRDT, reg);

	/* Setup USBPCR bits */
	reg = CSR_READ_4(sc, JZ_USBPCR);
	reg |= PCR_USB_MODE;
	reg |= PCR_COMMONONN;
	reg |= PCR_VBUSVLDEXT;
	reg |= PCR_VBUSVLDEXTSEL;
	reg &= ~(PCR_OTG_DISABLE);
	CSR_WRITE_4(sc, JZ_USBPCR, reg);

	/* Reset USB */
	reg = CSR_READ_4(sc, JZ_USBPCR);
	reg |= PCR_POR;
	CSR_WRITE_4(sc, JZ_USBPCR, reg);
	DELAY(1000);
	reg = CSR_READ_4(sc, JZ_USBPCR);
	reg &= ~(PCR_POR);
	CSR_WRITE_4(sc, JZ_USBPCR, reg);

	/* Unsuspend OTG port */
	reg = CSR_READ_4(sc, JZ_OPCR);
	reg |= OPCR_SPENDN0;
	CSR_WRITE_4(sc, JZ_OPCR, reg);

	CGU_UNLOCK(sc);
	return (0);
}
