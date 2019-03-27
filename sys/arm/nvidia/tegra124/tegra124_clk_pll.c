/*-
 * Copyright (c) 2016 Michal Meloun <mmel@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/rman.h>

#include <machine/bus.h>

#include <dev/extres/clk/clk.h>

#include <gnu/dts/include/dt-bindings/clock/tegra124-car.h>
#include "tegra124_car.h"

/* #define TEGRA_PLL_DEBUG */
#ifdef TEGRA_PLL_DEBUG
#define dprintf(...) printf(__VA_ARGS__)
#else
#define dprintf(...)
#endif

/* All PLLs. */
enum pll_type {
	PLL_M,
	PLL_X,
	PLL_C,
	PLL_C2,
	PLL_C3,
	PLL_C4,
	PLL_P,
	PLL_A,
	PLL_U,
	PLL_D,
	PLL_D2,
	PLL_DP,
	PLL_E,
	PLL_REFE};

/* Common base register bits. */
#define	PLL_BASE_BYPASS		(1U << 31)
#define	PLL_BASE_ENABLE		(1  << 30)
#define	PLL_BASE_REFDISABLE	(1  << 29)
#define	PLL_BASE_LOCK		(1  << 27)
#define	PLL_BASE_DIVM_SHIFT	0
#define	PLL_BASE_DIVN_SHIFT	8

#define	PLLRE_MISC_LOCK		(1 << 24)

#define	PLL_MISC_LOCK_ENABLE	(1 << 18)
#define	PLLC_MISC_LOCK_ENABLE	(1 << 24)
#define	PLLDU_MISC_LOCK_ENABLE	(1 << 22)
#define	PLLRE_MISC_LOCK_ENABLE	(1 << 30)
#define	PLLSS_MISC_LOCK_ENABLE	(1 << 30)

#define	PLLC_IDDQ_BIT		26
#define	PLLX_IDDQ_BIT		3
#define	PLLRE_IDDQ_BIT		16
#define	PLLSS_IDDQ_BIT		19

#define	PLL_LOCK_TIMEOUT	5000

/* Post divider <-> register value mapping. */
struct pdiv_table {
	uint32_t divider;	/* real divider */
	uint32_t value;		/* register value */
};

/* Bits definition of M, N and P fields. */
struct mnp_bits {
	uint32_t	m_width;
	uint32_t	n_width;
	uint32_t	p_width;
	uint32_t	p_shift;
};

struct clk_pll_def {
	struct clknode_init_def	clkdef;
	enum pll_type		type;
	uint32_t		base_reg;
	uint32_t		misc_reg;
	uint32_t		lock_mask;
	uint32_t		lock_enable;
	uint32_t		iddq_reg;
	uint32_t		iddq_mask;
	uint32_t		flags;
	struct pdiv_table 	*pdiv_table;
	struct mnp_bits		mnp_bits;
};

#define	PLL(_id, cname, pname)					\
	.clkdef.id = _id,					\
	.clkdef.name = cname,					\
	.clkdef.parent_names = (const char *[]){pname},		\
	.clkdef.parent_cnt = 1,				\
	.clkdef.flags = CLK_NODE_STATIC_STRINGS

/* Tegra K1 PLLs
 PLLM: Clock source for EMC 2x clock
 PLLX: Clock source for the fast CPU cluster and the shadow CPU
 PLLC: Clock source for general use
 PLLC2: Clock source for engine scaling
 PLLC3: Clock source for engine scaling
 PLLC4: Clock source for ISP/VI units
 PLLP: Clock source for most peripherals
 PLLA: Audio clock sources: (11.2896 MHz, 12.288 MHz, 24.576 MHz)
 PLLU: Clock source for USB PHY, provides 12/60/480 MHz
 PLLD: Clock sources for the DSI and display subsystem
 PLLD2: Clock sources for the DSI and display subsystem
 refPLLe:
 PLLE: generate the 100 MHz reference clock for USB 3.0 (spread spectrum)
 PLLDP: Clock source for eDP/LVDS (spread spectrum)

 DFLLCPU: DFLL clock source for the fast CPU cluster
 GPCPLL: Clock source for the GPU
*/

static struct pdiv_table pllm_map[] = {
	{1, 0},
	{2, 1},
	{0, 0}
};

static struct pdiv_table pllxc_map[] = {
	{ 1,  0},
	{ 2,  1},
	{ 3,  2},
	{ 4,  3},
	{ 5,  4},
	{ 6,  5},
	{ 8,  6},
	{10,  7},
	{12,  8},
	{16,  9},
	{12, 10},
	{16, 11},
	{20, 12},
	{24, 13},
	{32, 14},
	{ 0,  0}
};

static struct pdiv_table pllc_map[] = {
	{ 1, 0},
	{ 2, 1},
	{ 3, 2},
	{ 4, 3},
	{ 6, 4},
	{ 8, 5},
	{12, 6},
	{16, 7},
	{ 0,  0}
};

static struct pdiv_table pll12g_ssd_esd_map[] = {
	{ 1,  0},
	{ 2,  1},
	{ 3,  2},
	{ 4,  3},
	{ 5,  4},
	{ 6,  5},
	{ 8,  6},
	{10,  7},
	{12,  8},
	{16,  9},
	{12, 10},
	{16, 11},
	{20, 12},
	{24, 13},
	{32, 14},
	{ 0,  0}
};

static struct pdiv_table pllu_map[] = {
	{1, 1},
	{2, 0},
	{0, 0}
};

static struct pdiv_table pllrefe_map[] = {
	{1, 0},
	{2, 1},
	{3, 2},
	{4, 3},
	{5, 4},
	{6, 5},
	{0, 0},
};

static struct clk_pll_def pll_clks[] = {
/* PLLM: 880 MHz Clock source for EMC 2x clock */
	{
		PLL(TEGRA124_CLK_PLL_M, "pllM_out0", "osc_div_clk"),
		.type = PLL_M,
		.base_reg = PLLM_BASE,
		.misc_reg = PLLM_MISC,
		.lock_mask = PLL_BASE_LOCK,
		.lock_enable = PLL_MISC_LOCK_ENABLE,
		.pdiv_table = pllm_map,
		.mnp_bits = {8, 8, 1, 20},
	},
/* PLLX: 1GHz Clock source for the fast CPU cluster and the shadow CPU */
	{
		PLL(TEGRA124_CLK_PLL_X, "pllX_out", "osc_div_clk"),
		.type = PLL_X,
		.base_reg = PLLX_BASE,
		.misc_reg = PLLX_MISC,
		.lock_mask = PLL_BASE_LOCK,
		.lock_enable = PLL_MISC_LOCK_ENABLE,
		.iddq_reg = PLLX_MISC3,
		.iddq_mask = 1 << PLLX_IDDQ_BIT,
		.pdiv_table = pllxc_map,
		.mnp_bits = {8, 8, 4, 20},
	},
/* PLLC: 600 MHz Clock source for general use */
	{
		PLL(TEGRA124_CLK_PLL_C, "pllC_out0", "osc_div_clk"),
		.type = PLL_C,
		.base_reg = PLLC_BASE,
		.misc_reg = PLLC_MISC,
		.lock_mask = PLL_BASE_LOCK,
		.lock_enable = PLLC_MISC_LOCK_ENABLE,
		.iddq_reg = PLLC_MISC,
		.iddq_mask = 1 << PLLC_IDDQ_BIT,
		.pdiv_table = pllc_map,
		.mnp_bits = {8, 8, 4, 20},
	},
/* PLLC2: 600 MHz Clock source for engine scaling */
	{
		PLL(TEGRA124_CLK_PLL_C2, "pllC2_out0", "osc_div_clk"),
		.type = PLL_C2,
		.base_reg = PLLC2_BASE,
		.misc_reg = PLLC2_MISC,
		.lock_mask = PLL_BASE_LOCK,
		.lock_enable = PLL_MISC_LOCK_ENABLE,
		.pdiv_table = pllc_map,
		.mnp_bits = {2, 8, 3, 20},
	},
/* PLLC3: 600 MHz Clock source for engine scaling */
	{
		PLL(TEGRA124_CLK_PLL_C3, "pllC3_out0", "osc_div_clk"),
		.type = PLL_C3,
		.base_reg = PLLC3_BASE,
		.misc_reg = PLLC3_MISC,
		.lock_mask = PLL_BASE_LOCK,
		.lock_enable = PLL_MISC_LOCK_ENABLE,
		.pdiv_table = pllc_map,
		.mnp_bits = {2, 8, 3, 20},
	},
/* PLLC4: 600 MHz Clock source for ISP/VI units */
	{
		PLL(TEGRA124_CLK_PLL_C4, "pllC4_out0", "pllC4_src"),
		.type = PLL_C4,
		.base_reg = PLLC4_BASE,
		.misc_reg = PLLC4_MISC,
		.lock_mask = PLL_BASE_LOCK,
		.lock_enable = PLLSS_MISC_LOCK_ENABLE,
		.iddq_reg = PLLC4_BASE,
		.iddq_mask = 1 << PLLSS_IDDQ_BIT,
		.pdiv_table = pll12g_ssd_esd_map,
		.mnp_bits = {8, 8, 4, 20},
	},
/* PLLP: 408 MHz Clock source for most peripherals */
	{
		PLL(TEGRA124_CLK_PLL_P, "pllP_out0", "osc_div_clk"),
		.type = PLL_P,
		.base_reg = PLLP_BASE,
		.misc_reg = PLLP_MISC,
		.lock_mask = PLL_BASE_LOCK,
		.lock_enable = PLL_MISC_LOCK_ENABLE,
		.mnp_bits = {5, 10, 3,  20},
	},
/* PLLA: Audio clock sources: (11.2896 MHz, 12.288 MHz, 24.576 MHz) */
	{
		PLL(TEGRA124_CLK_PLL_A, "pllA_out", "pllP_out1"),
		.type = PLL_A,
		.base_reg = PLLA_BASE,
		.misc_reg = PLLA_MISC,
		.lock_mask = PLL_BASE_LOCK,
		.lock_enable = PLL_MISC_LOCK_ENABLE,
		.mnp_bits = {5, 10, 3,  20},
	},
/* PLLU: 480 MHz Clock source for USB PHY, provides 12/60/480 MHz */
	{
		PLL(TEGRA124_CLK_PLL_U, "pllU_out", "osc_div_clk"),
		.type = PLL_U,
		.base_reg = PLLU_BASE,
		.misc_reg = PLLU_MISC,
		.lock_mask = PLL_BASE_LOCK,
		.lock_enable = PLLDU_MISC_LOCK_ENABLE,
		.pdiv_table = pllu_map,
		.mnp_bits = {5, 10, 1, 20},
	},
/* PLLD: 600 MHz Clock sources for the DSI and display subsystem */
	{
		PLL(TEGRA124_CLK_PLL_D, "pllD_out", "osc_div_clk"),
		.type = PLL_D,
		.base_reg = PLLD_BASE,
		.misc_reg = PLLD_MISC,
		.lock_mask = PLL_BASE_LOCK,
		.lock_enable = PLL_MISC_LOCK_ENABLE,
		.mnp_bits = {5, 11, 3, 20},
	},
/* PLLD2: 600 MHz Clock sources for the DSI and display subsystem */
	{
		PLL(TEGRA124_CLK_PLL_D2, "pllD2_out", "pllD2_src"),
		.type = PLL_D2,
		.base_reg = PLLD2_BASE,
		.misc_reg = PLLD2_MISC,
		.lock_mask = PLL_BASE_LOCK,
		.lock_enable = PLLSS_MISC_LOCK_ENABLE,
		.iddq_reg = PLLD2_BASE,
		.iddq_mask =  1 << PLLSS_IDDQ_BIT,
		.pdiv_table = pll12g_ssd_esd_map,
		.mnp_bits = {8, 8, 4, 20},
	},
/* refPLLe:  */
	{
		PLL(0, "pllREFE_out", "osc_div_clk"),
		.type = PLL_REFE,
		.base_reg = PLLRE_BASE,
		.misc_reg = PLLRE_MISC,
		.lock_mask = PLLRE_MISC_LOCK,
		.lock_enable = PLLRE_MISC_LOCK_ENABLE,
		.iddq_reg = PLLRE_MISC,
		.iddq_mask = 1 << PLLRE_IDDQ_BIT,
		.pdiv_table = pllrefe_map,
		.mnp_bits = {8, 8, 4, 16},
	},
/* PLLE: generate the 100 MHz reference clock for USB 3.0 (spread spectrum) */
	{
		PLL(TEGRA124_CLK_PLL_E, "pllE_out0", "pllE_src"),
		.type = PLL_E,
		.base_reg = PLLE_BASE,
		.misc_reg = PLLE_MISC,
		.lock_mask = PLLE_MISC_LOCK,
		.lock_enable = PLLE_MISC_LOCK_ENABLE,
		.mnp_bits = {8, 8, 4, 24},
	},
/* PLLDP: 600 MHz Clock source for eDP/LVDS (spread spectrum) */
	{
		PLL(0, "pllDP_out0", "pllDP_src"),
		.type = PLL_DP,
		.base_reg = PLLDP_BASE,
		.misc_reg = PLLDP_MISC,
		.lock_mask = PLL_BASE_LOCK,
		.lock_enable = PLLSS_MISC_LOCK_ENABLE,
		.iddq_reg = PLLDP_BASE,
		.iddq_mask =  1 << PLLSS_IDDQ_BIT,
		.pdiv_table = pll12g_ssd_esd_map,
		.mnp_bits = {8, 8, 4, 20},
	},
};

static int tegra124_pll_init(struct clknode *clk, device_t dev);
static int tegra124_pll_set_gate(struct clknode *clk, bool enable);
static int tegra124_pll_recalc(struct clknode *clk, uint64_t *freq);
static int tegra124_pll_set_freq(struct clknode *clknode, uint64_t fin,
    uint64_t *fout, int flags, int *stop);
struct pll_sc {
	device_t		clkdev;
	enum pll_type		type;
	uint32_t		base_reg;
	uint32_t		misc_reg;
	uint32_t		lock_mask;
	uint32_t		lock_enable;
	uint32_t		iddq_reg;
	uint32_t		iddq_mask;
	uint32_t		flags;
	struct pdiv_table 	*pdiv_table;
	struct mnp_bits		mnp_bits;
};

static clknode_method_t tegra124_pll_methods[] = {
	/* Device interface */
	CLKNODEMETHOD(clknode_init,		tegra124_pll_init),
	CLKNODEMETHOD(clknode_set_gate,		tegra124_pll_set_gate),
	CLKNODEMETHOD(clknode_recalc_freq,	tegra124_pll_recalc),
	CLKNODEMETHOD(clknode_set_freq,		tegra124_pll_set_freq),
	CLKNODEMETHOD_END
};
DEFINE_CLASS_1(tegra124_pll, tegra124_pll_class, tegra124_pll_methods,
   sizeof(struct pll_sc), clknode_class);

static int
pll_enable(struct pll_sc *sc)
{
	uint32_t reg;


	RD4(sc, sc->base_reg, &reg);
	if (sc->type != PLL_E)
		reg &= ~PLL_BASE_BYPASS;
	reg |= PLL_BASE_ENABLE;
	WR4(sc, sc->base_reg, reg);
	return (0);
}

static int
pll_disable(struct pll_sc *sc)
{
	uint32_t reg;

	RD4(sc, sc->base_reg, &reg);
	if (sc->type != PLL_E)
		reg |= PLL_BASE_BYPASS;
	reg &= ~PLL_BASE_ENABLE;
	WR4(sc, sc->base_reg, reg);
	return (0);
}

static uint32_t
pdiv_to_reg(struct pll_sc *sc, uint32_t p_div)
{
	struct pdiv_table *tbl;

	tbl = sc->pdiv_table;
	if (tbl == NULL)
		return (ffs(p_div) - 1);

	while (tbl->divider != 0) {
		if (p_div <= tbl->divider)
			return (tbl->value);
		tbl++;
	}
	return (0xFFFFFFFF);
}

static uint32_t
reg_to_pdiv(struct pll_sc *sc, uint32_t reg)
{
	struct pdiv_table *tbl;

	tbl = sc->pdiv_table;
	if (tbl == NULL)
		return (1 << reg);

	while (tbl->divider) {
		if (reg == tbl->value)
			return (tbl->divider);
		tbl++;
	}
	return (0);
}

static uint32_t
get_masked(uint32_t val, uint32_t shift, uint32_t width)
{

	return ((val >> shift) & ((1 << width) - 1));
}

static uint32_t
set_masked(uint32_t val, uint32_t v, uint32_t shift, uint32_t width)
{

	val &= ~(((1 << width) - 1) << shift);
	val |= (v & ((1 << width) - 1)) << shift;
	return (val);
}

static void
get_divisors(struct pll_sc *sc, uint32_t *m, uint32_t *n, uint32_t *p)
{
	uint32_t val;
	struct mnp_bits *mnp_bits;

	mnp_bits = &sc->mnp_bits;
	RD4(sc, sc->base_reg, &val);
	*m = get_masked(val, PLL_BASE_DIVM_SHIFT, mnp_bits->m_width);
	*n = get_masked(val, PLL_BASE_DIVN_SHIFT, mnp_bits->n_width);
	*p = get_masked(val, mnp_bits->p_shift, mnp_bits->p_width);
}

static uint32_t
set_divisors(struct pll_sc *sc, uint32_t val, uint32_t m, uint32_t n,
    uint32_t p)
{
	struct mnp_bits *mnp_bits;

	mnp_bits = &sc->mnp_bits;
	val = set_masked(val, m, PLL_BASE_DIVM_SHIFT, mnp_bits->m_width);
	val = set_masked(val, n, PLL_BASE_DIVN_SHIFT, mnp_bits->n_width);
	val = set_masked(val, p, mnp_bits->p_shift, mnp_bits->p_width);
	return (val);
}

static bool
is_locked(struct pll_sc *sc)
{
	uint32_t reg;

	switch (sc->type) {
	case PLL_REFE:
		RD4(sc, sc->misc_reg, &reg);
		reg &=  PLLRE_MISC_LOCK;
		break;

	case PLL_E:
		RD4(sc, sc->misc_reg, &reg);
		reg &= PLLE_MISC_LOCK;
		break;

	default:
		RD4(sc, sc->base_reg, &reg);
		reg &= PLL_BASE_LOCK;
		break;
	}
	return (reg != 0);
}

static int
wait_for_lock(struct pll_sc *sc)
{
	int i;

	for (i = PLL_LOCK_TIMEOUT / 10; i > 0; i--) {
		if (is_locked(sc))
			break;
		DELAY(10);
	}
	if (i <= 0) {
		printf("PLL lock timeout\n");
		return (ETIMEDOUT);
	}
	return (0);
}

static int
plle_enable(struct pll_sc *sc)
{
	uint32_t reg;
	int rv;
	struct mnp_bits *mnp_bits;
	uint32_t pll_m = 1;
	uint32_t pll_n = 200;
	uint32_t pll_p = 13;
	uint32_t pll_cml = 13;

	mnp_bits = &sc->mnp_bits;


	/* Disable lock override. */
	RD4(sc, sc->base_reg, &reg);
	reg &= ~PLLE_BASE_LOCK_OVERRIDE;
	WR4(sc, sc->base_reg, reg);

	RD4(sc, PLLE_AUX, &reg);
	reg |= PLLE_AUX_ENABLE_SWCTL;
	reg &= ~PLLE_AUX_SEQ_ENABLE;
	WR4(sc, PLLE_AUX, reg);
	DELAY(10);

	RD4(sc, sc->misc_reg, &reg);
	reg |= PLLE_MISC_LOCK_ENABLE;
	reg |= PLLE_MISC_IDDQ_SWCTL;
	reg &= ~PLLE_MISC_IDDQ_OVERRIDE_VALUE;
	reg |= PLLE_MISC_PTS;
	reg |= PLLE_MISC_VREG_BG_CTRL_MASK;
	reg |= PLLE_MISC_VREG_CTRL_MASK;
	WR4(sc, sc->misc_reg, reg);
	DELAY(10);

	RD4(sc, PLLE_SS_CNTL, &reg);
	reg |= PLLE_SS_CNTL_DISABLE;
	WR4(sc, PLLE_SS_CNTL, reg);

	RD4(sc, sc->base_reg, &reg);
	reg = set_divisors(sc, reg, pll_m, pll_n, pll_p);
	reg &= ~(PLLE_BASE_DIVCML_MASK << PLLE_BASE_DIVCML_SHIFT);
	reg |= pll_cml << PLLE_BASE_DIVCML_SHIFT;
	WR4(sc, sc->base_reg, reg);
	DELAY(10);

	pll_enable(sc);
	rv = wait_for_lock(sc);
	if (rv != 0)
		return (rv);

	RD4(sc, PLLE_SS_CNTL, &reg);
	reg &= ~PLLE_SS_CNTL_SSCCENTER;
	reg &= ~PLLE_SS_CNTL_SSCINVERT;
	reg &= ~PLLE_SS_CNTL_COEFFICIENTS_MASK;
	reg |= PLLE_SS_CNTL_COEFFICIENTS_VAL;
	WR4(sc, PLLE_SS_CNTL, reg);
	reg &= ~PLLE_SS_CNTL_SSCBYP;
	reg &= ~PLLE_SS_CNTL_BYPASS_SS;
	WR4(sc, PLLE_SS_CNTL, reg);
	DELAY(10);

	reg &= ~PLLE_SS_CNTL_INTERP_RESET;
	WR4(sc, PLLE_SS_CNTL, reg);
	DELAY(10);

	/* HW control of brick pll. */
	RD4(sc, sc->misc_reg, &reg);
	reg &= ~PLLE_MISC_IDDQ_SWCTL;
	WR4(sc, sc->misc_reg, reg);

	RD4(sc, PLLE_AUX, &reg);
	reg |= PLLE_AUX_USE_LOCKDET;
	reg |= PLLE_AUX_SEQ_START_STATE;
	reg &= ~PLLE_AUX_ENABLE_SWCTL;
	reg &= ~PLLE_AUX_SS_SWCTL;
	WR4(sc, PLLE_AUX, reg);
	reg |= PLLE_AUX_SEQ_START_STATE;
	DELAY(10);
	reg |= PLLE_AUX_SEQ_ENABLE;
	WR4(sc, PLLE_AUX, reg);

	RD4(sc, XUSBIO_PLL_CFG0, &reg);
	reg |= XUSBIO_PLL_CFG0_PADPLL_USE_LOCKDET;
	reg |= XUSBIO_PLL_CFG0_SEQ_START_STATE;
	reg &= ~XUSBIO_PLL_CFG0_CLK_ENABLE_SWCTL;
	reg &= ~XUSBIO_PLL_CFG0_PADPLL_RESET_SWCTL;
	WR4(sc, XUSBIO_PLL_CFG0, reg);
	DELAY(10);

	reg |= XUSBIO_PLL_CFG0_SEQ_ENABLE;
	WR4(sc, XUSBIO_PLL_CFG0, reg);


	/* Enable HW control and unreset SATA PLL. */
	RD4(sc, SATA_PLL_CFG0, &reg);
	reg &= ~SATA_PLL_CFG0_PADPLL_RESET_SWCTL;
	reg &= ~SATA_PLL_CFG0_PADPLL_RESET_OVERRIDE_VALUE;
	reg |=  SATA_PLL_CFG0_PADPLL_USE_LOCKDET;
	reg &= ~SATA_PLL_CFG0_SEQ_IN_SWCTL;
	reg &= ~SATA_PLL_CFG0_SEQ_RESET_INPUT_VALUE;
	reg &= ~SATA_PLL_CFG0_SEQ_LANE_PD_INPUT_VALUE;
	reg &= ~SATA_PLL_CFG0_SEQ_PADPLL_PD_INPUT_VALUE;
	reg &= ~SATA_PLL_CFG0_SEQ_ENABLE;
	reg |=  SATA_PLL_CFG0_SEQ_START_STATE;
	WR4(sc, SATA_PLL_CFG0, reg);
	DELAY(10);
	reg |= SATA_PLL_CFG0_SEQ_ENABLE;
	WR4(sc, SATA_PLL_CFG0, reg);

	/* Enable HW control of PCIe PLL. */
	RD4(sc, PCIE_PLL_CFG0, &reg);
	reg |= PCIE_PLL_CFG0_SEQ_ENABLE;
	WR4(sc, PCIE_PLL_CFG0, reg);

	return (0);
}

static int
tegra124_pll_set_gate(struct clknode *clknode, bool enable)
{
	int rv;
	struct pll_sc *sc;

	sc = clknode_get_softc(clknode);
	if (enable == 0) {
		rv = pll_disable(sc);
		return(rv);
	}

	if (sc->type == PLL_E)
		rv = plle_enable(sc);
	else
		rv = pll_enable(sc);
	return (rv);
}

static int
pll_set_std(struct pll_sc *sc, uint64_t fin, uint64_t *fout, int flags,
    uint32_t m, uint32_t n, uint32_t p)
{
	uint32_t reg;
	struct mnp_bits *mnp_bits;
	int rv;

	mnp_bits = &sc->mnp_bits;
	if (m >= (1 << mnp_bits->m_width))
		return (ERANGE);
	if (n >= (1 << mnp_bits->n_width))
		return (ERANGE);
	if (pdiv_to_reg(sc, p) >= (1 << mnp_bits->p_width))
		return (ERANGE);

	if (flags & CLK_SET_DRYRUN) {
		if (((flags & (CLK_SET_ROUND_UP | CLK_SET_ROUND_DOWN)) == 0) &&
		    (*fout != (((fin / m) * n) /p)))
			return (ERANGE);

		*fout = ((fin / m) * n) /p;

		return (0);
	}

	pll_disable(sc);

	/* take pll out of IDDQ */
	if (sc->iddq_reg != 0)
		MD4(sc, sc->iddq_reg, sc->iddq_mask, 0);

	RD4(sc, sc->base_reg, &reg);
	reg = set_masked(reg, m, PLL_BASE_DIVM_SHIFT, mnp_bits->m_width);
	reg = set_masked(reg, n, PLL_BASE_DIVN_SHIFT, mnp_bits->n_width);
	reg = set_masked(reg, pdiv_to_reg(sc, p), mnp_bits->p_shift,
	    mnp_bits->p_width);
	WR4(sc, sc->base_reg, reg);

	/* Enable PLL. */
	RD4(sc, sc->base_reg, &reg);
	reg |= PLL_BASE_ENABLE;
	WR4(sc, sc->base_reg, reg);

	/* Enable lock detection. */
	RD4(sc, sc->misc_reg, &reg);
	reg |= sc->lock_enable;
	WR4(sc, sc->misc_reg, reg);

	rv = wait_for_lock(sc);
	if (rv != 0) {
		/* Disable PLL */
		RD4(sc, sc->base_reg, &reg);
		reg &= ~PLL_BASE_ENABLE;
		WR4(sc, sc->base_reg, reg);
		return (rv);
	}
	RD4(sc, sc->misc_reg, &reg);

	pll_enable(sc);
	*fout = ((fin / m) * n) / p;
	return 0;
}

static int
plla_set_freq(struct pll_sc *sc, uint64_t fin, uint64_t *fout, int flags)
{
	uint32_t m, n, p;

	p = 1;
	m = 5;
	n = (*fout * p * m + fin / 2)/ fin;
	dprintf("%s: m: %d, n: %d, p: %d\n", __func__, m, n, p);
	return (pll_set_std(sc,  fin, fout, flags, m, n, p));
}

static int
pllc_set_freq(struct pll_sc *sc, uint64_t fin, uint64_t *fout, int flags)
{
	uint32_t m, n, p;

	p = 2;
	m = 1;
	n = (*fout * p * m + fin / 2)/ fin;
	dprintf("%s: m: %d, n: %d, p: %d\n", __func__, m, n, p);
	return (pll_set_std( sc, fin, fout, flags, m, n, p));
}

/*
 * PLLD2 is used as source for pixel clock for HDMI.
 * We must be able to set it frequency very flexibly and
 * precisely (within 5% tolerance limit allowed by HDMI specs).
 *
 * For this reason, it is necessary to search the full state space.
 * Fortunately, thanks to early cycle terminations, performance
 * is within acceptable limits.
 */
#define	PLLD2_PFD_MIN		  12000000 	/*  12 MHz */
#define	PLLD2_PFD_MAX		  38000000	/*  38 MHz */
#define	PLLD2_VCO_MIN	  	 600000000	/* 600 MHz */
#define	PLLD2_VCO_MAX		1200000000	/* 1.2 GHz */

static int
plld2_set_freq(struct pll_sc *sc, uint64_t fin, uint64_t *fout, int flags)
{
	uint32_t m, n, p;
	uint32_t best_m, best_n, best_p;
	uint64_t vco, pfd;
	int64_t err, best_err;
	struct mnp_bits *mnp_bits;
	struct pdiv_table *tbl;
	int p_idx, rv;

	mnp_bits = &sc->mnp_bits;
	tbl = sc->pdiv_table;
	best_err = INT64_MAX;

	for (p_idx = 0; tbl[p_idx].divider != 0; p_idx++) {
		p = tbl[p_idx].divider;

		/* Check constraints */
		vco = *fout * p;
		if (vco < PLLD2_VCO_MIN)
			continue;
		if (vco > PLLD2_VCO_MAX)
			break;

		for (m = 1; m < (1 << mnp_bits->m_width); m++) {
			n = (*fout * p * m + fin / 2) / fin;

			/* Check constraints */
			if (n == 0)
				continue;
			if (n >= (1 << mnp_bits->n_width))
				break;
			vco = (fin * n) / m;
			if (vco > PLLD2_VCO_MAX || vco < PLLD2_VCO_MIN)
				continue;
			pfd = fin / m;
			if (pfd > PLLD2_PFD_MAX || vco < PLLD2_PFD_MIN)
				continue;

			/* Constraints passed, save best result */
			err = *fout - vco / p;
			if (err < 0)
				err = -err;
			if (err < best_err) {
				best_err = err;
				best_p = p;
				best_m = m;
				best_n = n;
			}
			if (err == 0)
				goto done;
		}
	}
done:
	/*
	 * HDMI specification allows 5% pixel clock tolerance,
	 * we will by a slightly stricter
	 */
	if (best_err > ((*fout * 100) / 4))
		return (ERANGE);

	if (flags & CLK_SET_DRYRUN)
		return (0);
	rv = pll_set_std(sc, fin, fout, flags, best_m, best_n, best_p);
	/* XXXX Panic for rv == ERANGE ? */
	return (rv);
}

static int
pllrefe_set_freq(struct pll_sc *sc, uint64_t fin, uint64_t *fout, int flags)
{
	uint32_t m, n, p;

	m = 1;
	p = 1;
	n = *fout * p * m / fin;
	dprintf("%s: m: %d, n: %d, p: %d\n", __func__, m, n, p);
	return (pll_set_std(sc, fin, fout, flags, m, n, p));
}

static int
pllx_set_freq(struct pll_sc *sc, uint64_t fin, uint64_t *fout, int flags)
{
	uint32_t reg;
	uint32_t m, n, p;
	struct mnp_bits *mnp_bits;
	int rv;

	mnp_bits = &sc->mnp_bits;

	p = 1;
	m = 1;
	n = (*fout * p * m + fin / 2)/ fin;
	dprintf("%s: m: %d, n: %d, p: %d\n", __func__, m, n, p);

	if (m >= (1 << mnp_bits->m_width))
		return (ERANGE);
	if (n >= (1 << mnp_bits->n_width))
		return (ERANGE);
	if (pdiv_to_reg(sc, p) >= (1 << mnp_bits->p_width))
		return (ERANGE);

	if (flags & CLK_SET_DRYRUN) {
		if (((flags & (CLK_SET_ROUND_UP | CLK_SET_ROUND_DOWN)) == 0) &&
		    (*fout != (((fin / m) * n) /p)))
			return (ERANGE);
		*fout = ((fin / m) * n) /p;
		return (0);
	}

	/* PLLX doesn't have bypass, disable it first. */
	RD4(sc, sc->base_reg, &reg);
	reg &= ~PLL_BASE_ENABLE;
	WR4(sc, sc->base_reg, reg);

	/* Set PLL. */
	RD4(sc, sc->base_reg, &reg);
	reg = set_masked(reg, m, PLL_BASE_DIVM_SHIFT, mnp_bits->m_width);
	reg = set_masked(reg, n, PLL_BASE_DIVN_SHIFT, mnp_bits->n_width);
	reg = set_masked(reg, pdiv_to_reg(sc, p), mnp_bits->p_shift,
	    mnp_bits->p_width);
	WR4(sc, sc->base_reg, reg);
	RD4(sc, sc->base_reg, &reg);
	DELAY(100);

	/* Enable lock detection. */
	RD4(sc, sc->misc_reg, &reg);
	reg |= sc->lock_enable;
	WR4(sc, sc->misc_reg, reg);

	/* Enable PLL. */
	RD4(sc, sc->base_reg, &reg);
	reg |= PLL_BASE_ENABLE;
	WR4(sc, sc->base_reg, reg);

	rv = wait_for_lock(sc);
	if (rv != 0) {
		/* Disable PLL */
		RD4(sc, sc->base_reg, &reg);
		reg &= ~PLL_BASE_ENABLE;
		WR4(sc, sc->base_reg, reg);
		return (rv);
	}
	RD4(sc, sc->misc_reg, &reg);

	*fout = ((fin / m) * n) / p;
	return (0);
}

static int
tegra124_pll_set_freq(struct clknode *clknode, uint64_t fin, uint64_t *fout,
    int flags, int *stop)
{
	*stop = 1;
	int rv;
	struct pll_sc *sc;

	sc = clknode_get_softc(clknode);
	dprintf("%s: %s requested freq: %llu, input freq: %llu\n", __func__,
	   clknode_get_name(clknode), *fout, fin);
	switch (sc->type) {
	case PLL_A:
		rv = plla_set_freq(sc, fin, fout, flags);
		break;
	case PLL_C:
		rv = pllc_set_freq(sc, fin, fout, flags);
		break;
	case PLL_D2:
		rv = plld2_set_freq(sc, fin, fout, flags);
		break;

	case PLL_REFE:
		rv = pllrefe_set_freq(sc, fin, fout, flags);
		break;

	case PLL_X:
		rv = pllx_set_freq(sc, fin, fout, flags);
		break;

	case PLL_U:
		if (*fout == 480000000)  /* PLLU is fixed to 480 MHz */
			rv = 0;
		else
			rv = ERANGE;
		break;
	default:
		rv = ENXIO;
		break;
	}

	return (rv);
}


static int
tegra124_pll_init(struct clknode *clk, device_t dev)
{
	struct pll_sc *sc;
	uint32_t reg;

	sc = clknode_get_softc(clk);

	/* If PLL is enabled, enable lock detect too. */
	RD4(sc, sc->base_reg, &reg);
	if (reg & PLL_BASE_ENABLE) {
		RD4(sc, sc->misc_reg, &reg);
		reg |= sc->lock_enable;
		WR4(sc, sc->misc_reg, reg);
	}
	if (sc->type == PLL_REFE) {
		RD4(sc, sc->misc_reg, &reg);
		reg &= ~(1 << 29);	/* Diasble lock override */
		WR4(sc, sc->misc_reg, reg);
	}

	clknode_init_parent_idx(clk, 0);
	return(0);
}

static int
tegra124_pll_recalc(struct clknode *clk, uint64_t *freq)
{
	struct pll_sc *sc;
	uint32_t m, n, p, pr;
	uint32_t reg, misc_reg;
	int locked;

	sc = clknode_get_softc(clk);

	RD4(sc, sc->base_reg, &reg);
	RD4(sc, sc->misc_reg, &misc_reg);

	get_divisors(sc, &m, &n, &pr);
	if (sc->type != PLL_E)
		p = reg_to_pdiv(sc, pr);
	else
		p = 2 * (pr - 1);
	locked = is_locked(sc);

	dprintf("%s: %s (0x%08x, 0x%08x) - m: %d, n: %d, p: %d (%d): "
	    "e: %d, r: %d, o: %d - %s\n", __func__,
	    clknode_get_name(clk), reg, misc_reg, m, n, p, pr,
	    (reg >> 30) & 1, (reg >> 29) & 1, (reg >> 28) & 1,
	    locked ? "locked" : "unlocked");

	if ((m == 0) || (n == 0) || (p == 0)) {
		*freq = 0;
		return (EINVAL);
	}
	*freq = ((*freq / m) * n) / p;
	return (0);
}

static int
pll_register(struct clkdom *clkdom, struct clk_pll_def *clkdef)
{
	struct clknode *clk;
	struct pll_sc *sc;

	clk = clknode_create(clkdom, &tegra124_pll_class, &clkdef->clkdef);
	if (clk == NULL)
		return (ENXIO);

	sc = clknode_get_softc(clk);
	sc->clkdev = clknode_get_device(clk);
	sc->type = clkdef->type;
	sc->base_reg = clkdef->base_reg;
	sc->misc_reg = clkdef->misc_reg;
	sc->lock_mask = clkdef->lock_mask;
	sc->lock_enable = clkdef->lock_enable;
	sc->iddq_reg = clkdef->iddq_reg;
	sc->iddq_mask = clkdef->iddq_mask;
	sc->flags = clkdef->flags;
	sc->pdiv_table = clkdef->pdiv_table;
	sc->mnp_bits = clkdef->mnp_bits;
	clknode_register(clkdom, clk);
	return (0);
}

static void config_utmi_pll(struct tegra124_car_softc *sc)
{
	uint32_t reg;
	/*
	 * XXX Simplified UTMIP settings for 12MHz base clock.
	 */
#define	ENABLE_DELAY_COUNT 	0x02
#define	STABLE_COUNT		0x2F
#define	ACTIVE_DELAY_COUNT	0x04
#define	XTAL_FREQ_COUNT		0x76

	CLKDEV_READ_4(sc->dev, UTMIP_PLL_CFG2, &reg);
	reg &= ~UTMIP_PLL_CFG2_STABLE_COUNT(~0);
	reg |= UTMIP_PLL_CFG2_STABLE_COUNT(STABLE_COUNT);
	reg &= ~UTMIP_PLL_CFG2_ACTIVE_DLY_COUNT(~0);
	reg |= UTMIP_PLL_CFG2_ACTIVE_DLY_COUNT(ACTIVE_DELAY_COUNT);
	reg &= ~UTMIP_PLL_CFG2_FORCE_PD_SAMP_A_POWERDOWN;
	reg &= ~UTMIP_PLL_CFG2_FORCE_PD_SAMP_B_POWERDOWN;
	reg &= ~UTMIP_PLL_CFG2_FORCE_PD_SAMP_C_POWERDOWN;
	CLKDEV_WRITE_4(sc->dev, UTMIP_PLL_CFG2, reg);

	CLKDEV_READ_4(sc->dev, UTMIP_PLL_CFG1, &reg);
	reg &= ~UTMIP_PLL_CFG1_ENABLE_DLY_COUNT(~0);
	reg |= UTMIP_PLL_CFG1_ENABLE_DLY_COUNT(ENABLE_DELAY_COUNT);
	reg &= ~UTMIP_PLL_CFG1_XTAL_FREQ_COUNT(~0);
	reg |= UTMIP_PLL_CFG1_XTAL_FREQ_COUNT(XTAL_FREQ_COUNT);
	reg &= ~UTMIP_PLL_CFG1_FORCE_PLL_ENABLE_POWERDOWN;
	reg &= ~UTMIP_PLL_CFG1_FORCE_PLL_ACTIVE_POWERDOWN;
	reg &= ~UTMIP_PLL_CFG1_FORCE_PLLU_POWERUP;
	reg &= ~UTMIP_PLL_CFG1_FORCE_PLLU_POWERDOWN;
	CLKDEV_WRITE_4(sc->dev, UTMIP_PLL_CFG1, reg);

	/* Prepare UTMIP requencer. */
	CLKDEV_READ_4(sc->dev, UTMIPLL_HW_PWRDN_CFG0, &reg);
	reg |= UTMIPLL_HW_PWRDN_CFG0_USE_LOCKDET;
	reg &= ~UTMIPLL_HW_PWRDN_CFG0_CLK_ENABLE_SWCTL;
	reg |= UTMIPLL_HW_PWRDN_CFG0_SEQ_START_STATE;
	CLKDEV_WRITE_4(sc->dev, UTMIPLL_HW_PWRDN_CFG0, reg);

	/* Powerup UTMIP. */
	CLKDEV_READ_4(sc->dev, UTMIP_PLL_CFG1, &reg);
	reg &= ~UTMIP_PLL_CFG1_FORCE_PLL_ENABLE_POWERUP;
	reg &= ~UTMIP_PLL_CFG1_FORCE_PLL_ENABLE_POWERDOWN;
	CLKDEV_WRITE_4(sc->dev, UTMIP_PLL_CFG1, reg);
	DELAY(10);

	/* SW override for UTMIPLL */
	CLKDEV_READ_4(sc->dev, UTMIPLL_HW_PWRDN_CFG0, &reg);
	reg |= UTMIPLL_HW_PWRDN_CFG0_IDDQ_SWCTL;
	reg &= ~UTMIPLL_HW_PWRDN_CFG0_IDDQ_OVERRIDE;
	CLKDEV_WRITE_4(sc->dev, UTMIPLL_HW_PWRDN_CFG0, reg);
	DELAY(10);

	/* HW control of UTMIPLL. */
	CLKDEV_READ_4(sc->dev, UTMIPLL_HW_PWRDN_CFG0, &reg);
	reg |= UTMIPLL_HW_PWRDN_CFG0_SEQ_ENABLE;
	CLKDEV_WRITE_4(sc->dev, UTMIPLL_HW_PWRDN_CFG0, reg);
}

void
tegra124_init_plls(struct tegra124_car_softc *sc)
{
	int i, rv;

	for (i = 0; i < nitems(pll_clks); i++) {
		rv = pll_register(sc->clkdom, pll_clks + i);
		if (rv != 0)
			panic("pll_register failed");
	}
	config_utmi_pll(sc);

}
