/*	$OpenBSD: rkclock.c,v 1.93 2025/05/17 13:29:49 kettenis Exp $	*/
/*
 * Copyright (c) 2017, 2018 Mark Kettenis <kettenis@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/device.h>

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

/* RK3288 registers */
#define RK3288_CRU_APLL_CON(i)		(0x0000 + (i) * 4)
#define RK3288_CRU_CPLL_CON(i)		(0x0020 + (i) * 4)
#define RK3288_CRU_GPLL_CON(i)		(0x0030 + (i) * 4)
#define RK3288_CRU_NPLL_CON(i)		(0x0040 + (i) * 4)
#define  RK3288_CRU_PLL_CLKR_MASK		(0x3f << 8)
#define  RK3288_CRU_PLL_CLKR_SHIFT		8
#define  RK3288_CRU_PLL_CLKOD_MASK		(0xf << 0)
#define  RK3288_CRU_PLL_CLKOD_SHIFT		0
#define  RK3288_CRU_PLL_CLKF_MASK		(0x1fff << 0)
#define  RK3288_CRU_PLL_CLKF_SHIFT		0
#define  RK3288_CRU_PLL_RESET			(1 << 5)
#define RK3288_CRU_MODE_CON		0x0050
#define  RK3288_CRU_MODE_PLL_WORK_MODE_MASK	0x3
#define  RK3288_CRU_MODE_PLL_WORK_MODE_SLOW	0x0
#define  RK3288_CRU_MODE_PLL_WORK_MODE_NORMAL	0x1
#define RK3288_CRU_CLKSEL_CON(i)	(0x0060 + (i) * 4)
#define RK3288_CRU_SOFTRST_CON(i)	(0x01b8 + (i) * 4)

/* RK3308 registers */
#define RK3308_CRU_APLL_CON(i)		(0x0000 + (i) * 4)
#define RK3308_CRU_DPLL_CON(i)		(0x0020 + (i) * 4)
#define RK3308_CRU_VPLL0_CON(i)		(0x0040 + (i) * 4)
#define RK3308_CRU_VPLL1_CON(i)		(0x0060 + (i) * 4)
#define  RK3308_CRU_PLL_POSTDIV1_MASK		(0x7 << 12)
#define  RK3308_CRU_PLL_POSTDIV1_SHIFT		12
#define  RK3308_CRU_PLL_FBDIV_MASK		(0xfff << 0)
#define  RK3308_CRU_PLL_FBDIV_SHIFT		0
#define  RK3308_CRU_PLL_DSMPD			(1 << 12)
#define  RK3308_CRU_PLL_PLL_LOCK		(1 << 10)
#define  RK3308_CRU_PLL_POSTDIV2_MASK		(0x7 << 6)
#define  RK3308_CRU_PLL_POSTDIV2_SHIFT		6
#define  RK3308_CRU_PLL_REFDIV_MASK		(0x3f << 0)
#define  RK3308_CRU_PLL_REFDIV_SHIFT		0
#define  RK3308_CRU_PLL_FRACDIV_MASK		(0xffffff << 0)
#define  RK3308_CRU_PLL_FRACDIV_SHIFT		0
#define RK3308_CRU_CRU_MODE		0x00a0
#define RK3308_CRU_CRU_MODE_MASK	0x3
#define RK3308_CRU_CRU_MODE_SLOW	0x0
#define RK3308_CRU_CRU_MODE_NORMAL	0x1
#define RK3308_CRU_CRU_MODE_DEEP	0x2
#define RK3308_CRU_CLKSEL_CON(i)	(0x0100 + (i) * 4)
#define  RK3308_CRU_ACLK_CORE_DIV_CON_MASK	(0x07 << 12)
#define  RK3308_CRU_ACLK_CORE_DIV_CON_SHIFT	12
#define  RK3308_CRU_CLK_CORE_DBG_DIV_CON_MASK	(0x0f << 8)
#define  RK3308_CRU_CLK_CORE_DBG_DIV_CON_SHIFT  8
#define  RK3308_CRU_CORE_CLK_PLL_SEL_MASK	(0x03 << 6)
#define  RK3308_CRU_CORE_CLK_PLL_SEL_SHIFT	6
#define  RK3308_CRU_CLK_CORE_DIV_CON_MASK	(0x0f << 0)
#define  RK3308_CRU_CLK_CORE_DIV_CON_SHIFT	0
#define RK3308_CRU_CLKGATE_CON(i)	(0x0300 + (i) * 4)
#define RK3308_CRU_SOFTRST_CON(i)	(0x0400 + (i) * 4)

/* RK3328 registers */
#define RK3328_CRU_APLL_CON(i)		(0x0000 + (i) * 4)
#define RK3328_CRU_DPLL_CON(i)		(0x0020 + (i) * 4)
#define RK3328_CRU_CPLL_CON(i)		(0x0040 + (i) * 4)
#define RK3328_CRU_GPLL_CON(i)		(0x0060 + (i) * 4)
#define RK3328_CRU_NPLL_CON(i)		(0x00a0 + (i) * 4)
#define  RK3328_CRU_PLL_POSTDIV1_MASK		(0x7 << 12)
#define  RK3328_CRU_PLL_POSTDIV1_SHIFT		12
#define  RK3328_CRU_PLL_FBDIV_MASK		(0xfff << 0)
#define  RK3328_CRU_PLL_FBDIV_SHIFT		0
#define  RK3328_CRU_PLL_DSMPD			(1 << 12)
#define  RK3328_CRU_PLL_PLL_LOCK		(1 << 10)
#define  RK3328_CRU_PLL_POSTDIV2_MASK		(0x7 << 6)
#define  RK3328_CRU_PLL_POSTDIV2_SHIFT		6
#define  RK3328_CRU_PLL_REFDIV_MASK		(0x3f << 0)
#define  RK3328_CRU_PLL_REFDIV_SHIFT		0
#define  RK3328_CRU_PLL_FRACDIV_MASK		(0xffffff << 0)
#define  RK3328_CRU_PLL_FRACDIV_SHIFT		0
#define RK3328_CRU_CRU_MODE		0x0080
#define  RK3328_CRU_CRU_MODE_MASK		0x1
#define  RK3328_CRU_CRU_MODE_SLOW		0x0
#define  RK3328_CRU_CRU_MODE_NORMAL		0x1
#define RK3328_CRU_CLKSEL_CON(i)	(0x0100 + (i) * 4)
#define  RK3328_CRU_CORE_CLK_PLL_SEL_MASK	(0x3 << 6)
#define  RK3328_CRU_CORE_CLK_PLL_SEL_SHIFT	6
#define  RK3328_CRU_CLK_CORE_DIV_CON_MASK	(0x1f << 0)
#define  RK3328_CRU_CLK_CORE_DIV_CON_SHIFT	0
#define  RK3328_CRU_ACLK_CORE_DIV_CON_MASK	(0x7 << 4)
#define  RK3328_CRU_ACLK_CORE_DIV_CON_SHIFT	4
#define  RK3328_CRU_CLK_CORE_DBG_DIV_CON_MASK	(0xf << 0)
#define  RK3328_CRU_CLK_CORE_DBG_DIV_CON_SHIFT	0
#define  RK3328_CRU_VOP_DCLK_SRC_SEL_MASK	(0x1 << 1)
#define  RK3328_CRU_VOP_DCLK_SRC_SEL_SHIFT	1
#define RK3328_CRU_CLKGATE_CON(i)	(0x0200 + (i) * 4)
#define RK3328_CRU_SOFTRST_CON(i)	(0x0300 + (i) * 4)

#define RK3328_GRF_SOC_CON4		0x0410
#define  RK3328_GRF_GMAC2IO_MAC_CLK_OUTPUT_EN	(1 << 14)
#define RK3328_GRF_MAC_CON1		0x0904
#define  RK3328_GRF_GMAC2IO_RMII_EXTCLK_SEL	(1 << 10)

/* RK3399 registers */
#define RK3399_CRU_LPLL_CON(i)		(0x0000 + (i) * 4)
#define RK3399_CRU_BPLL_CON(i)		(0x0020 + (i) * 4)
#define RK3399_CRU_DPLL_CON(i)		(0x0020 + (i) * 4)
#define RK3399_CRU_CPLL_CON(i)		(0x0060 + (i) * 4)
#define RK3399_CRU_GPLL_CON(i)		(0x0080 + (i) * 4)
#define RK3399_CRU_NPLL_CON(i)		(0x00a0 + (i) * 4)
#define RK3399_CRU_VPLL_CON(i)		(0x00c0 + (i) * 4)
#define  RK3399_CRU_PLL_FBDIV_MASK		(0xfff << 0)
#define  RK3399_CRU_PLL_FBDIV_SHIFT		0
#define  RK3399_CRU_PLL_POSTDIV2_MASK		(0x7 << 12)
#define  RK3399_CRU_PLL_POSTDIV2_SHIFT		12
#define  RK3399_CRU_PLL_POSTDIV1_MASK		(0x7 << 8)
#define  RK3399_CRU_PLL_POSTDIV1_SHIFT		8
#define  RK3399_CRU_PLL_REFDIV_MASK		(0x3f << 0)
#define  RK3399_CRU_PLL_REFDIV_SHIFT		0
#define  RK3399_CRU_PLL_PLL_WORK_MODE_MASK	(0x3 << 8)
#define  RK3399_CRU_PLL_PLL_WORK_MODE_SLOW	(0x0 << 8)
#define  RK3399_CRU_PLL_PLL_WORK_MODE_NORMAL	(0x1 << 8)
#define  RK3399_CRU_PLL_PLL_WORK_MODE_DEEP_SLOW	(0x2 << 8)
#define  RK3399_CRU_PLL_PLL_LOCK		(1U << 31)
#define RK3399_CRU_CLKSEL_CON(i)	(0x0100 + (i) * 4)
#define  RK3399_CRU_ACLKM_CORE_DIV_CON_MASK	(0x1f << 8)
#define  RK3399_CRU_ACLKM_CORE_DIV_CON_SHIFT	8
#define  RK3399_CRU_CORE_PLL_SEL_MASK		(0x3 << 6)
#define  RK3399_CRU_CORE_PLL_SEL_APLL		(0x0 << 6)
#define  RK3399_CRU_CORE_PLL_SEL_BPLL		(0x1 << 6)
#define  RK3399_CRU_CORE_PLL_SEL_DPLL		(0x2 << 6)
#define  RK3399_CRU_CORE_PLL_SEL_GPLL		(0x3 << 6)
#define  RK3399_CRU_CORE_PLL_SEL_SHIFT		6
#define  RK3399_CRU_CLK_CORE_DIV_CON_MASK	(0x1f << 0)
#define  RK3399_CRU_CLK_CORE_DIV_CON_SHIFT	0
#define  RK3399_CRU_PCLK_DBG_DIV_CON_MASK	(0x1f << 8)
#define  RK3399_CRU_PCLK_DBG_DIV_CON_SHIFT	8
#define  RK3399_CRU_ATCLK_CORE_DIV_CON_MASK	(0x1f << 0)
#define  RK3399_CRU_ATCLK_CORE_DIV_CON_SHIFT	0
#define  RK3399_CRU_CLK_SD_PLL_SEL_MASK		(0x7 << 8)
#define  RK3399_CRU_CLK_SD_PLL_SEL_SHIFT	8
#define  RK3399_CRU_CLK_SD_DIV_CON_MASK		(0x7f << 0)
#define  RK3399_CRU_CLK_SD_DIV_CON_SHIFT	0
#define RK3399_CRU_CLKGATE_CON(i)	(0x0300 + (i) * 4)
#define RK3399_CRU_SOFTRST_CON(i)	(0x0400 + (i) * 4)
#define RK3399_CRU_SDMMC_CON(i)		(0x0580 + (i) * 4)

#define RK3399_PMUCRU_PPLL_CON(i)	(0x0000 + (i) * 4)
#define RK3399_PMUCRU_CLKSEL_CON(i)	(0x0080 + (i) * 4)

/* RK3528 registers */
#define RK3528_CRU_PLL_CON(i)		(0x00000 + (i) * 4)
#define RK3528_CRU_CLKSEL_CON(i)	(0x00300 + (i) * 4)
#define RK3528_CRU_GATE_CON(i)		(0x00800 + (i) * 4)
#define RK3528_CRU_SOFTRST_CON(i)	(0x00a00 + (i) * 4)
#define RK3528_PCIE_CRU_PLL_CON(i)	(0x20000 + (i) * 4)

/* RK3568 registers */
#define RK3568_CRU_APLL_CON(i)		(0x0000 + (i) * 4)
#define RK3568_CRU_DPLL_CON(i)		(0x0020 + (i) * 4)
#define RK3568_CRU_GPLL_CON(i)		(0x0040 + (i) * 4)
#define RK3568_CRU_CPLL_CON(i)		(0x0060 + (i) * 4)
#define RK3568_CRU_NPLL_CON(i)		(0x0080 + (i) * 4)
#define RK3568_CRU_VPLL_CON(i)		(0x00a0 + (i) * 4)
#define RK3568_CRU_MODE_CON		0x00c0
#define RK3568_CRU_CLKSEL_CON(i)	(0x0100 + (i) * 4)
#define RK3568_CRU_GATE_CON(i)		(0x0300 + (i) * 4)
#define RK3568_CRU_SOFTRST_CON(i)	(0x0400 + (i) * 4)

#define RK3568_PMUCRU_PPLL_CON(i)	(0x0000 + (i) * 4)
#define RK3568_PMUCRU_HPLL_CON(i)	(0x0040 + (i) * 4)
#define RK3568_PMUCRU_MODE_CON		0x0080
#define RK3568_PMUCRU_CLKSEL_CON(i)	(0x0100 + (i) * 4)
#define RK3568_PMUCRU_GATE_CON(i)	(0x0180 + (i) * 4)

/* RK3588 registers */
#define RK3588_CRU_AUPLL_CON(i)		(0x00180 + (i) * 4)
#define RK3588_CRU_CPLL_CON(i)		(0x001a0 + (i) * 4)
#define RK3588_CRU_GPLL_CON(i)		(0x001c0 + (i) * 4)
#define RK3588_CRU_NPLL_CON(i)		(0x001e0 + (i) * 4)
#define  RK3588_CRU_PLL_M_MASK			(0x3ff << 0)
#define  RK3588_CRU_PLL_M_SHIFT			0
#define  RK3588_CRU_PLL_RESETB			(1 << 13)
#define  RK3588_CRU_PLL_S_MASK			(0x7 << 6)
#define  RK3588_CRU_PLL_S_SHIFT			6
#define  RK3588_CRU_PLL_P_MASK			(0x3f << 0)
#define  RK3588_CRU_PLL_P_SHIFT			0
#define  RK3588_CRU_PLL_K_MASK			(0xffff << 0)
#define  RK3588_CRU_PLL_K_SHIFT			0
#define  RK3588_CRU_PLL_PLL_LOCK		(1 << 15)
#define RK3588_CRU_MODE_CON		0x00280
#define  RK3588_CRU_MODE_MASK			0x3
#define  RK3588_CRU_MODE_SLOW			0x0
#define  RK3588_CRU_MODE_NORMAL			0x1

#define RK3588_CRU_CLKSEL_CON(i)	(0x00300 + (i) * 4)
#define RK3588_CRU_GATE_CON(i)		(0x00800 + (i) * 4)
#define RK3588_CRU_SOFTRST_CON(i)	(0x00a00 + (i) * 4)

#define RK3588_PHPTOPCRU_PPLL_CON(i)	(0x08200 + (i) * 4)
#define RK3588_PHPTOPCRU_SOFTRST_CON(i)	(0x08a00 + (i) * 4)
#define RK3588_PMUCRU_CLKSEL_CON(i)	(0x30300 + (i) * 4)

#include "rkclock_clocks.h"

struct rkclock {
	uint16_t idx;
	uint32_t reg;
	uint16_t sel_mask;
	uint16_t div_mask;
	uint16_t parents[8];
	uint32_t flags;
};

#define SEL(l, f)	(((1 << (l - f + 1)) - 1) << f)
#define DIV(l, f)	SEL(l, f)

#define FIXED_PARENT	(1 << 0)
#define SET_PARENT	(1 << 1)

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct rkclock_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	struct regmap		*sc_grf;

	uint32_t		sc_phandle;
	const struct rkclock	*sc_clocks;

	struct clock_device	sc_cd;
	struct reset_device	sc_rd;
};

int rkclock_match(struct device *, void *, void *);
void rkclock_attach(struct device *, struct device *, void *);

const struct cfattach	rkclock_ca = {
	sizeof (struct rkclock_softc), rkclock_match, rkclock_attach
};

struct cfdriver rkclock_cd = {
	NULL, "rkclock", DV_DULL
};

void	rk3288_init(struct rkclock_softc *);
uint32_t rk3288_get_frequency(void *, uint32_t *);
int	rk3288_set_frequency(void *, uint32_t *, uint32_t);
void	rk3288_enable(void *, uint32_t *, int);
void	rk3288_reset(void *, uint32_t *, int);

void	rk3308_init(struct rkclock_softc *);
uint32_t rk3308_get_frequency(void *, uint32_t *);
int	rk3308_set_frequency(void *, uint32_t *, uint32_t);
int	rk3308_set_parent(void *, uint32_t *, uint32_t *);
void	rk3308_enable(void *, uint32_t *, int);
void	rk3308_reset(void *, uint32_t *, int);

void	rk3328_init(struct rkclock_softc *);
uint32_t rk3328_get_frequency(void *, uint32_t *);
int	rk3328_set_frequency(void *, uint32_t *, uint32_t);
int	rk3328_set_parent(void *, uint32_t *, uint32_t *);
void	rk3328_enable(void *, uint32_t *, int);
void	rk3328_reset(void *, uint32_t *, int);

void	rk3399_init(struct rkclock_softc *);
uint32_t rk3399_get_frequency(void *, uint32_t *);
int	rk3399_set_frequency(void *, uint32_t *, uint32_t);
int	rk3399_set_parent(void *, uint32_t *, uint32_t *);
void	rk3399_enable(void *, uint32_t *, int);
void	rk3399_reset(void *, uint32_t *, int);

void	rk3399_pmu_init(struct rkclock_softc *);
uint32_t rk3399_pmu_get_frequency(void *, uint32_t *);
int	rk3399_pmu_set_frequency(void *, uint32_t *, uint32_t);
void	rk3399_pmu_enable(void *, uint32_t *, int);
void	rk3399_pmu_reset(void *, uint32_t *, int);

void	rk3528_init(struct rkclock_softc *);
uint32_t rk3528_get_frequency(void *, uint32_t *);
int	rk3528_set_frequency(void *, uint32_t *, uint32_t);
int	rk3528_set_parent(void *, uint32_t *, uint32_t *);
void	rk3528_enable(void *, uint32_t *, int);
void	rk3528_reset(void *, uint32_t *, int);

void	rk3568_init(struct rkclock_softc *);
uint32_t rk3568_get_frequency(void *, uint32_t *);
int	rk3568_set_frequency(void *, uint32_t *, uint32_t);
int	rk3568_set_parent(void *, uint32_t *, uint32_t *);
void	rk3568_enable(void *, uint32_t *, int);
void	rk3568_reset(void *, uint32_t *, int);

void	rk3568_pmu_init(struct rkclock_softc *);
uint32_t rk3568_pmu_get_frequency(void *, uint32_t *);
int	rk3568_pmu_set_frequency(void *, uint32_t *, uint32_t);
void	rk3568_pmu_enable(void *, uint32_t *, int);
void	rk3568_pmu_reset(void *, uint32_t *, int);

void	rk3588_init(struct rkclock_softc *);
uint32_t rk3588_get_frequency(void *, uint32_t *);
int	rk3588_set_frequency(void *, uint32_t *, uint32_t);
void	rk3588_enable(void *, uint32_t *, int);
void	rk3588_reset(void *, uint32_t *, int);

struct rkclock_compat {
	const char *compat;
	const char *name;
	int	assign;
	void	(*init)(struct rkclock_softc *);
	void	(*enable)(void *, uint32_t *, int);
	uint32_t (*get_frequency)(void *, uint32_t *);
	int	(*set_frequency)(void *, uint32_t *, uint32_t);
	int	(*set_parent)(void *, uint32_t *, uint32_t *);
	void	(*reset)(void *, uint32_t *, int);
};

const struct rkclock_compat rkclock_compat[] = {
	{
		"rockchip,rk3288-cru", NULL, 0, rk3288_init,
		rk3288_enable, rk3288_get_frequency,
		rk3288_set_frequency, NULL,
		rk3288_reset
	},
	{
		"rockchip,rk3308-cru", NULL, 1, rk3308_init,
		rk3308_enable, rk3308_get_frequency,
		rk3308_set_frequency, rk3308_set_parent,
		rk3308_reset
	},
	{
		"rockchip,rk3328-cru", NULL, 1, rk3328_init,
		rk3328_enable, rk3328_get_frequency,
		rk3328_set_frequency, rk3328_set_parent,
		rk3328_reset
	},
	{
		"rockchip,rk3399-cru", NULL, 1, rk3399_init,
		rk3399_enable, rk3399_get_frequency,
		rk3399_set_frequency, rk3399_set_parent,
		rk3399_reset
	},
	{
		"rockchip,rk3399-pmucru", NULL, 1, rk3399_pmu_init,
		rk3399_pmu_enable, rk3399_pmu_get_frequency,
		rk3399_pmu_set_frequency, NULL,
		rk3399_pmu_reset
	},
	{
		"rockchip,rk3528-cru", NULL, 1, rk3528_init,
		rk3528_enable, rk3528_get_frequency,
		rk3528_set_frequency, rk3528_set_parent,
		rk3528_reset
	},
	{
		"rockchip,rk3568-cru", "CRU", 1, rk3568_init,
		rk3568_enable, rk3568_get_frequency,
		rk3568_set_frequency, rk3568_set_parent,
		rk3568_reset
	},
	{
		"rockchip,rk3568-pmucru", "PMUCRU", 1, rk3568_pmu_init,
		rk3568_pmu_enable, rk3568_pmu_get_frequency,
		rk3568_pmu_set_frequency, NULL,
		rk3568_pmu_reset
	},
	{
		"rockchip,rk3588-cru", NULL, 1, rk3588_init,
		rk3588_enable, rk3588_get_frequency,
		rk3588_set_frequency, NULL,
		rk3588_reset
	},
};

int
rkclock_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;
	int i;

	for (i = 0; i < nitems(rkclock_compat); i++) {
		if (OF_is_compatible(faa->fa_node, rkclock_compat[i].compat))
			return 10;
	}

	return 0;
}

void
rkclock_attach(struct device *parent, struct device *self, void *aux)
{
	struct rkclock_softc *sc = (struct rkclock_softc *)self;
	struct fdt_attach_args *faa = aux;
	uint32_t grf;
	int i;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	grf = OF_getpropint(faa->fa_node, "rockchip,grf", 0);
	sc->sc_grf = regmap_byphandle(grf);

	sc->sc_phandle = OF_getpropint(faa->fa_node, "phandle", 0);

	for (i = 0; i < nitems(rkclock_compat); i++) {
		if (OF_is_compatible(faa->fa_node, rkclock_compat[i].compat)) {
			break;
		}
	}
	KASSERT(i < nitems(rkclock_compat));

	if (rkclock_compat[i].name != NULL)
		printf(": %s", rkclock_compat[i].name);

	printf("\n");

	if (rkclock_compat[i].init)
		rkclock_compat[i].init(sc);

	sc->sc_cd.cd_node = faa->fa_node;
	sc->sc_cd.cd_cookie = sc;
	sc->sc_cd.cd_enable = rkclock_compat[i].enable;
	sc->sc_cd.cd_get_frequency = rkclock_compat[i].get_frequency;
	sc->sc_cd.cd_set_frequency = rkclock_compat[i].set_frequency;
	sc->sc_cd.cd_set_parent = rkclock_compat[i].set_parent;
	clock_register(&sc->sc_cd);

	sc->sc_rd.rd_node = faa->fa_node;
	sc->sc_rd.rd_cookie = sc;
	sc->sc_rd.rd_reset = rkclock_compat[i].reset;
	reset_register(&sc->sc_rd);

	if (rkclock_compat[i].assign)
		clock_set_assigned(faa->fa_node);
}

const struct rkclock *
rkclock_lookup(struct rkclock_softc *sc, uint32_t idx)
{
	const struct rkclock *clk;

	for (clk = sc->sc_clocks; clk->idx; clk++) {
		if (clk->idx == idx)
			return clk;
	}

	return NULL;
}

uint32_t
rkclock_external_frequency(const char *name)
{
	char buf[64] = {};
	int len, node;

	/*
	 * Hunt through the device tree to find a fixed-rate clock
	 * that has the requested clock output signal name.  This may
	 * be too simple.
	 */
	node = OF_peer(0);
	for (node = OF_child(node); node != 0; node = OF_peer(node)) {
		len = OF_getproplen(node, "clock-output-names");
		if (len <= 0 || len > sizeof(buf))
			continue;
		OF_getprop(node, "clock-output-names", buf, sizeof(buf));
		if (strcmp(buf, name) != 0)
			continue;
		if (OF_is_compatible(node, "fixed-clock"))
			return OF_getpropint(node, "clock-frequency", 0);
	}

	return 0;
}

uint32_t
rkclock_div_con(struct rkclock_softc *sc, const struct rkclock *clk,
    uint32_t mux, uint32_t freq)
{
	uint32_t parent_freq, div, div_con, max_div_con;
	uint32_t idx = clk->parents[mux];

	/* Derive maximum value from mask. */
	max_div_con = clk->div_mask >> (ffs(clk->div_mask) - 1);
	
	parent_freq = sc->sc_cd.cd_get_frequency(sc, &idx);
	div = (parent_freq + freq - 1) / freq;
	div_con = (div > 0 ? div - 1 : 0);
	return (div_con < max_div_con) ? div_con : max_div_con;
}

uint32_t
rkclock_freq(struct rkclock_softc *sc, const struct rkclock *clk,
    uint32_t mux, uint32_t freq)
{
	uint32_t parent_freq, div_con;
	uint32_t idx = clk->parents[mux];

	parent_freq = sc->sc_cd.cd_get_frequency(sc, &idx);
	div_con = rkclock_div_con(sc, clk, mux, freq);
	return parent_freq / (div_con + 1);
}

uint32_t
rkclock_get_frequency(struct rkclock_softc *sc, uint32_t idx)
{
	const struct rkclock *clk;
	uint32_t reg, mux, div_con;
	int shift;

	clk = rkclock_lookup(sc, idx);
	if (clk == NULL) {
		printf("%s(%s, %u)\n", __func__, sc->sc_dev.dv_xname, idx);
		return 0;
	}

	reg = HREAD4(sc, clk->reg);
	shift = ffs(clk->sel_mask) - 1;
	if (shift == -1)
		mux = 0;
	else
		mux = (reg & clk->sel_mask) >> shift;
	shift = ffs(clk->div_mask) - 1;
	if (shift == -1)
		div_con = 0;
	else
		div_con = (reg & clk->div_mask) >> shift;

	if (clk->parents[mux] == 0) {
		printf("%s: parent 0x%08x\n", __func__, idx);
		return 0;
	}
	idx = clk->parents[mux];
	return sc->sc_cd.cd_get_frequency(sc, &idx) / (div_con + 1);
}

int
rkclock_set_frequency(struct rkclock_softc *sc, uint32_t idx, uint32_t freq)
{
	const struct rkclock *clk;
	uint32_t reg, mux, div_con;
	uint32_t best_freq, best_mux, f;
	uint32_t parent;
	int sel_shift, div_shift, i;

	clk = rkclock_lookup(sc, idx);
	if (clk == NULL) {
		printf("%s(%s, %u, %u)\n", __func__, sc->sc_dev.dv_xname,
		    idx, freq);
		return -1;
	}

	reg = HREAD4(sc, clk->reg);
	sel_shift = ffs(clk->sel_mask) - 1;
	if (sel_shift == -1)
		mux = sel_shift = 0;
	else
		mux = (reg & clk->sel_mask) >> sel_shift;

	if (clk->parents[mux] == 0) {
		printf("%s(%s, %u, %u) parent\n", __func__,
		    sc->sc_dev.dv_xname, idx, freq);
		return 0;
	}

	if (clk->flags & SET_PARENT) {
		parent = clk->parents[mux];
		sc->sc_cd.cd_set_frequency(sc, &parent, freq);
		if (clk->div_mask == 0)
			return 0;
	}

	/*
	 * If there is no divider, pick the parent with the frequency
	 * closest to the target frequency.
	 */
	if (clk->div_mask == 0) {
		/*
		 * Start out with the current parent.  This prevents
		 * unnecessary switching to a different parent.
		 */
		parent = clk->parents[mux];
		best_freq = sc->sc_cd.cd_get_frequency(sc, &parent);
		best_mux = mux;

		for (i = 0; i < nitems(clk->parents); i++) {
			if (clk->parents[i] == 0)
				continue;
			parent = clk->parents[i];
			f = sc->sc_cd.cd_get_frequency(sc, &parent);
			if ((best_freq > freq && f < best_freq) ||
			    (f > best_freq && f <= freq)) {
				best_freq = f;
				best_mux = i;
			}
		}

		HWRITE4(sc, clk->reg,
		    clk->sel_mask << 16 | best_mux << sel_shift);
		return 0;
	}

	/*
	 * Start out with the current parent.  This prevents
	 * unnecessary switching to a different parent.
	 */
	best_freq = rkclock_freq(sc, clk, mux, freq);
	best_mux = mux;

	/*
	 * Find the parent that allows configuration of a frequency
	 * closest to the target frequency.
	 */
	if ((clk->flags & FIXED_PARENT) == 0) {
		for (i = 0; i < nitems(clk->parents); i++) {
			if (clk->parents[i] == 0)
				continue;
			f = rkclock_freq(sc, clk, i, freq);
			if ((best_freq > freq && f < best_freq) ||
			    (f > best_freq && f <= freq)) {
				best_freq = f;
				best_mux = i;
			}
		}
	}

	div_con = rkclock_div_con(sc, clk, best_mux, freq);
	div_shift = ffs(clk->div_mask) - 1;
	HWRITE4(sc, clk->reg,
	    clk->sel_mask << 16 | best_mux << sel_shift |
	    clk->div_mask << 16 | div_con << div_shift);
	return 0;
}

int
rkclock_set_parent(struct rkclock_softc *sc, uint32_t idx, uint32_t parent)
{
	const struct rkclock *clk;
	uint32_t mux;
	int shift;

	clk = rkclock_lookup(sc, idx);
	if (clk == NULL || clk->sel_mask == 0) {
		printf("%s: 0x%08x\n", __func__, idx);
		return -1;
	}

	for (mux = 0; mux < nitems(clk->parents); mux++) {
		if (clk->parents[mux] == parent)
			break;
	}
	if (mux == nitems(clk->parents) || parent == 0) {
		printf("%s: 0x%08x parent 0x%08x\n", __func__, idx, parent);
		return -1;
	}

	shift = ffs(clk->sel_mask) - 1;
	HWRITE4(sc, clk->reg, clk->sel_mask << 16 | mux << shift);
	return 0;
}

/*
 * Rockchip RK3288
 */

const struct rkclock rk3288_clocks[] = {
	{
		RK3288_CLK_SDMMC, RK3288_CRU_CLKSEL_CON(11),
		SEL(7, 6), DIV(5, 0),
		{ RK3288_PLL_CPLL, RK3288_PLL_GPLL, RK3288_XIN24M }
	}
};

void
rk3288_init(struct rkclock_softc *sc)
{
	int node;

	/*
	 * Since the hardware comes up with a really conservative CPU
	 * clock frequency, and U-Boot doesn't set it to a more
	 * reasonable default, try to do so here.  These defaults were
	 * chosen assuming that the CPU voltage is at least 1.1 V.
	 * Only do this on the Tinker-RK3288 for now where this is
	 * likely to be true given the default voltages for the
	 * regulators on that board.
	 */
	node = OF_finddevice("/");
	if (OF_is_compatible(node, "rockchip,rk3288-tinker")) {
		uint32_t idx;
		
		/* Run at 1.2 GHz. */
		idx = RK3288_ARMCLK;
		rk3288_set_frequency(sc, &idx, 1200000000);
	}

	sc->sc_clocks = rk3288_clocks;
}

uint32_t
rk3288_get_pll(struct rkclock_softc *sc, bus_size_t base)
{
	uint32_t clkod, clkr, clkf;
	uint32_t reg;

	reg = HREAD4(sc, base);
	clkod = (reg & RK3288_CRU_PLL_CLKOD_MASK) >>
	    RK3288_CRU_PLL_CLKOD_SHIFT;
	clkr = (reg & RK3288_CRU_PLL_CLKR_MASK) >>
	    RK3288_CRU_PLL_CLKR_SHIFT;
	reg = HREAD4(sc, base + 4);
	clkf = (reg & RK3288_CRU_PLL_CLKF_MASK) >>
	    RK3288_CRU_PLL_CLKF_SHIFT;
	return 24000000ULL * (clkf + 1) / (clkr + 1) / (clkod + 1);
}

int
rk3288_set_pll(struct rkclock_softc *sc, bus_size_t base, uint32_t freq)
{
	int shift = 4 * (base / RK3288_CRU_CPLL_CON(0));
	uint32_t no, nr, nf;

	/*
	 * It is not clear whether all combinations of the clock
	 * dividers result in a stable clock.  Therefore this function
	 * only supports a limited set of PLL clock rates.  For now
	 * this set covers all the CPU frequencies supported by the
	 * Linux kernel.
	 */
	switch (freq) {
	case 1800000000:
	case 1704000000:
	case 1608000000:
	case 1512000000:
	case 1488000000:
	case 1416000000:
	case 1200000000:
		nr = no = 1;
		break;
	case 1008000000:
	case 816000000:
	case 696000000:
	case 600000000:
		nr = 1; no = 2;
		break;
	case 408000000:
	case 312000000:
		nr = 1; no = 4;
		break;
	case 216000000:
	case 126000000:
		nr = 1; no = 8;
		break;
	default:
		printf("%s: %u Hz\n", __func__, freq);
		return -1;
	}

	/* Calculate feedback divider. */
	nf = freq * nr * no / 24000000;

	/*
	 * Select slow mode to guarantee a stable clock while we're
	 * adjusting the PLL.
	 */
	HWRITE4(sc, RK3288_CRU_MODE_CON,
	    (RK3288_CRU_MODE_PLL_WORK_MODE_MASK << 16 |
	     RK3288_CRU_MODE_PLL_WORK_MODE_SLOW) << shift);

	/* Assert reset. */
	HWRITE4(sc, base + 0x000c,
	    RK3288_CRU_PLL_RESET << 16 | RK3288_CRU_PLL_RESET);

	/* Set PLL rate. */
	HWRITE4(sc, base + 0x0000,
	    RK3288_CRU_PLL_CLKR_MASK << 16 |
	    (nr - 1) << RK3288_CRU_PLL_CLKR_SHIFT |
	    RK3288_CRU_PLL_CLKOD_MASK << 16 |
	    (no - 1) << RK3288_CRU_PLL_CLKOD_SHIFT);
	HWRITE4(sc, base + 0x0004,
	    RK3288_CRU_PLL_CLKF_MASK << 16 |
	    (nf - 1) << RK3288_CRU_PLL_CLKF_SHIFT);

	/* Deassert reset and wait. */
	HWRITE4(sc, base + 0x000c,
	    RK3288_CRU_PLL_RESET << 16);
	delay((nr * 500 / 24) + 1);

	/* Switch back to normal mode. */
	HWRITE4(sc, RK3288_CRU_MODE_CON,
	    (RK3288_CRU_MODE_PLL_WORK_MODE_MASK << 16 |
	     RK3288_CRU_MODE_PLL_WORK_MODE_NORMAL) << shift);

	return 0;
}

uint32_t
rk3288_get_frequency(void *cookie, uint32_t *cells)
{
	struct rkclock_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t reg, mux, div_con, aclk_div_con;

	switch (idx) {
	case RK3288_PLL_APLL:
		return rk3288_get_pll(sc, RK3288_CRU_APLL_CON(0));
	case RK3288_PLL_CPLL:
		return rk3288_get_pll(sc, RK3288_CRU_CPLL_CON(0));
	case RK3288_PLL_GPLL:
		return rk3288_get_pll(sc, RK3288_CRU_GPLL_CON(0));
	case RK3288_PLL_NPLL:
		return rk3288_get_pll(sc, RK3288_CRU_NPLL_CON(0));
	case RK3288_ARMCLK:
		reg = HREAD4(sc, RK3288_CRU_CLKSEL_CON(0));
		mux = (reg >> 15) & 0x1;
		div_con = (reg >> 8) & 0x1f;
		idx = (mux == 0) ? RK3288_PLL_APLL : RK3288_PLL_GPLL;
		return rk3288_get_frequency(sc, &idx) / (div_con + 1);
	case RK3288_XIN24M:
		return 24000000;
	case RK3288_CLK_UART0:
		reg = HREAD4(sc, RK3288_CRU_CLKSEL_CON(13));
		mux = (reg >> 8) & 0x3;
		div_con = reg & 0x7f;
		if (mux == 2)
			return 24000000 / (div_con + 1);
		break;
	case RK3288_CLK_UART1:
		reg = HREAD4(sc, RK3288_CRU_CLKSEL_CON(14));
		mux = (reg >> 8) & 0x3;
		div_con = reg & 0x7f;
		if (mux == 2)
			return 24000000 / (div_con + 1);
		break;
	case RK3288_CLK_UART2:
		reg = HREAD4(sc, RK3288_CRU_CLKSEL_CON(15));
		mux = (reg >> 8) & 0x3;
		div_con = reg & 0x7f;
		if (mux == 2)
			return 24000000 / (div_con + 1);
		break;
	case RK3288_CLK_UART3:
		reg = HREAD4(sc, RK3288_CRU_CLKSEL_CON(16));
		mux = (reg >> 8) & 0x3;
		div_con = reg & 0x7f;
		if (mux == 2)
			return 24000000 / (div_con + 1);
		break;
	case RK3288_CLK_UART4:
		reg = HREAD4(sc, RK3288_CRU_CLKSEL_CON(3));
		mux = (reg >> 8) & 0x3;
		div_con = reg & 0x7f;
		if (mux == 2)
			return 24000000 / (div_con + 1);
		break;
	case RK3288_CLK_MAC:
		reg = HREAD4(sc, RK3288_CRU_CLKSEL_CON(21));
		if (reg & 0x10)
			return 125000000;
		mux = (reg >> 0) & 0x3;
		div_con = (reg >> 8) & 0x1f;
		switch (mux) {
		case 0:
			idx = RK3288_PLL_NPLL;
			break;
		case 1:
			idx = RK3288_PLL_CPLL;
			break;
		case 2:
			idx = RK3288_PLL_GPLL;
			break;
		default:
			return 0;
		}
		return rk3288_get_frequency(sc, &idx) / (div_con + 1);
	case RK3288_PCLK_I2C0:
	case RK3288_PCLK_I2C2:
		reg = HREAD4(sc, RK3288_CRU_CLKSEL_CON(1));
		mux = (reg >> 15) & 0x1;
		/* pd_bus_pclk_div_con */
		div_con = (reg >> 12) & 0x7;
		if (mux == 1)
			idx = RK3288_PLL_GPLL;
		else
			idx = RK3288_PLL_CPLL;
		return rk3288_get_frequency(sc, &idx) / (div_con + 1);
	case RK3288_PCLK_I2C1:
	case RK3288_PCLK_I2C3:
	case RK3288_PCLK_I2C4:
	case RK3288_PCLK_I2C5:
		reg = HREAD4(sc, RK3288_CRU_CLKSEL_CON(10));
		mux = (reg >> 15) & 0x1;
		/* peri_pclk_div_con */
		div_con = (reg >> 12) & 0x3;
		/* peri_aclk_div_con */
		aclk_div_con = reg & 0xf;
		if (mux == 1)
			idx = RK3288_PLL_GPLL;
		else
			idx = RK3288_PLL_CPLL;
		return (rk3288_get_frequency(sc, &idx) / (aclk_div_con + 1)) >>
		    div_con;
	default:
		break;
	}

	return rkclock_get_frequency(sc, idx);
}

int
rk3288_set_frequency(void *cookie, uint32_t *cells, uint32_t freq)
{
	struct rkclock_softc *sc = cookie;
	uint32_t idx = cells[0];
	int error;

	switch (idx) {
	case RK3288_PLL_APLL:
		return rk3288_set_pll(sc, RK3288_CRU_APLL_CON(0), freq);
	case RK3288_ARMCLK:
		idx = RK3288_PLL_APLL;
		error = rk3288_set_frequency(sc, &idx, freq);
		if (error == 0) {
			HWRITE4(sc, RK3288_CRU_CLKSEL_CON(0),
			    ((1 << 15) | (0x1f << 8)) << 16);
		}
		return error;
	default:
		break;
	}

	return rkclock_set_frequency(sc, idx, freq);
}

void
rk3288_enable(void *cookie, uint32_t *cells, int on)
{
	uint32_t idx = cells[0];

	switch (idx) {
	case RK3288_CLK_SDMMC:
	case RK3288_CLK_TSADC:
	case RK3288_CLK_UART0:
	case RK3288_CLK_UART1:
	case RK3288_CLK_UART2:
	case RK3288_CLK_UART3:
	case RK3288_CLK_UART4:
	case RK3288_CLK_MAC_RX:
	case RK3288_CLK_MAC_TX:
	case RK3288_CLK_SDMMC_DRV:
	case RK3288_CLK_SDMMC_SAMPLE:
	case RK3288_CLK_MAC:
	case RK3288_ACLK_GMAC:
	case RK3288_PCLK_GMAC:
	case RK3288_PCLK_I2C0:
	case RK3288_PCLK_I2C1:
	case RK3288_PCLK_I2C2:
	case RK3288_PCLK_I2C3:
	case RK3288_PCLK_I2C4:
	case RK3288_PCLK_I2C5:
	case RK3288_PCLK_TSADC:
	case RK3288_HCLK_HOST0:
	case RK3288_HCLK_SDMMC:
		/* Enabled by default. */
		break;
	default:
		printf("%s: 0x%08x\n", __func__, idx);
		break;
	}
}

void
rk3288_reset(void *cookie, uint32_t *cells, int on)
{
	struct rkclock_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t mask = (1 << (idx % 16));

	HWRITE4(sc, RK3288_CRU_SOFTRST_CON(idx / 16),
	    mask << 16 | (on ? mask : 0));
}

/*
 * Rockchip RK3308
 */

const struct rkclock rk3308_clocks[] = {
	{
		RK3308_CLK_RTC32K, RK3308_CRU_CLKSEL_CON(2),
		SEL(10, 9), 0,
		{ RK3308_PLL_VPLL0, RK3308_PLL_VPLL1 }
	},
	{
		RK3308_CLK_UART0, RK3308_CRU_CLKSEL_CON(10),
		SEL(15, 13), DIV(4, 0),
		{ RK3308_PLL_DPLL, RK3308_PLL_VPLL0, RK3308_PLL_VPLL1,
		  RK3308_USB480M, RK3308_XIN24M }
	},
	{
		RK3308_CLK_UART1, RK3308_CRU_CLKSEL_CON(13),
		SEL(15, 13), DIV(4, 0),
		{ RK3308_PLL_DPLL, RK3308_PLL_VPLL0, RK3308_PLL_VPLL1,
		  RK3308_USB480M, RK3308_XIN24M }
	},
	{
		RK3308_CLK_UART2, RK3308_CRU_CLKSEL_CON(16),
		SEL(15, 13), DIV(4, 0),
		{ RK3308_PLL_DPLL, RK3308_PLL_VPLL0, RK3308_PLL_VPLL1,
		  RK3308_USB480M, RK3308_XIN24M }
	},
	{
		RK3308_CLK_UART3, RK3308_CRU_CLKSEL_CON(19),
		SEL(15, 13), DIV(4, 0),
		{ RK3308_PLL_DPLL, RK3308_PLL_VPLL0, RK3308_PLL_VPLL1,
		  RK3308_USB480M, RK3308_XIN24M }
	},
	{
		RK3308_CLK_UART4, RK3308_CRU_CLKSEL_CON(22),
		SEL(15, 13), DIV(4, 0),
		{ RK3308_PLL_DPLL, RK3308_PLL_VPLL0, RK3308_PLL_VPLL1,
		  RK3308_USB480M, RK3308_XIN24M }
	},
	{
		RK3308_CLK_PWM0, RK3308_CRU_CLKSEL_CON(29),
		SEL(15, 14), DIV(6, 0),
		{ RK3308_PLL_DPLL, RK3308_PLL_VPLL0, RK3308_XIN24M }
	},
	{
		RK3308_CLK_SPI0, RK3308_CRU_CLKSEL_CON(30),
		SEL(15, 14), DIV(6, 0),
		{ RK3308_PLL_DPLL, RK3308_PLL_VPLL0, RK3308_XIN24M }
	},
	{
		RK3308_CLK_SPI1, RK3308_CRU_CLKSEL_CON(31),
		SEL(15, 14), DIV(6, 0),
		{ RK3308_PLL_DPLL, RK3308_PLL_VPLL0, RK3308_XIN24M }
	},
	{
		RK3308_CLK_SPI2, RK3308_CRU_CLKSEL_CON(32),
		SEL(15, 14), DIV(6, 0),
		{ RK3308_PLL_DPLL, RK3308_PLL_VPLL0, RK3308_XIN24M }
	},
	{
		RK3308_CLK_TSADC, RK3308_CRU_CLKSEL_CON(33),
		0, DIV(10, 0),
		{ RK3308_XIN24M }
	},
	{
		RK3308_CLK_SARADC, RK3308_CRU_CLKSEL_CON(34),
		0, DIV(10, 0),
		{ RK3308_XIN24M }
	},
	{
		RK3308_CLK_CRYPTO, RK3308_CRU_CLKSEL_CON(7),
		SEL(7, 6), DIV(4, 0),
		{ RK3308_PLL_DPLL, RK3308_PLL_VPLL0, RK3308_PLL_VPLL1, 0 }
	},
	{
		RK3308_CLK_CRYPTO_APK, RK3308_CRU_CLKSEL_CON(7),
		SEL(15, 14), DIV(12, 8),
		{ RK3308_PLL_DPLL, RK3308_PLL_VPLL0, RK3308_PLL_VPLL1, 0 }
	},
	{
		RK3308_CLK_SDMMC, RK3308_CRU_CLKSEL_CON(39),
		SEL(9, 8), DIV(7, 0),
		{ RK3308_PLL_DPLL, RK3308_PLL_VPLL0, RK3308_PLL_VPLL1,
		  RK3308_XIN24M }
	},
	{
		RK3308_CLK_SDIO, RK3308_CRU_CLKSEL_CON(40),
		SEL(9, 8), DIV(7, 0),
		{ RK3308_PLL_DPLL, RK3308_PLL_VPLL0, RK3308_PLL_VPLL1,
		  RK3308_XIN24M }
	},
	{
		RK3308_CLK_EMMC, RK3308_CRU_CLKSEL_CON(41),
		SEL(9, 8), DIV(7, 0),
		{ RK3308_PLL_DPLL, RK3308_PLL_VPLL0, RK3308_PLL_VPLL1,
		  RK3308_XIN24M }
	},
	{
		RK3308_CLK_MAC_SRC, RK3308_CRU_CLKSEL_CON(43),
		SEL(7, 6), DIV(4, 0),
		{ RK3308_PLL_DPLL, RK3308_PLL_VPLL0, RK3308_PLL_VPLL1, 0 }
	},
	{
		RK3308_CLK_MAC, RK3308_CRU_CLKSEL_CON(43),
		SEL(14, 13), 0,
		{ RK3308_CLK_MAC_SRC, 0 },
		SET_PARENT
	},
	{
		RK3308_ACLK_PERI_SRC, RK3308_CRU_CLKSEL_CON(36),
		SEL(7, 6), 0,
		{ RK3308_PLL_DPLL, RK3308_PLL_VPLL0, RK3308_PLL_VPLL1, 0 }
	},
	{
		RK3308_PCLK_PERI, RK3308_CRU_CLKSEL_CON(37),
		0, DIV(12, 8),
		{ RK3308_ACLK_PERI_SRC }
	},
	{
		RK3308_PCLK_MAC, 0, 0, 0,
		{ RK3308_PCLK_PERI }
	},
	
	{
		/* Sentinel */
	}
};

void
rk3308_init(struct rkclock_softc *sc)
{
	int i;

	/* The code below assumes all clocks are enabled.  Check this!. */
	for (i = 0; i <= 14; i++) {
		if (HREAD4(sc, RK3308_CRU_CLKGATE_CON(i)) != 0x00000000) {
			printf("CRU_CLKGATE_CON%d: 0x%08x\n", i,
			    HREAD4(sc, RK3308_CRU_CLKGATE_CON(i)));
		}
	}
	sc->sc_clocks = rk3308_clocks;
}

uint32_t
rk3308_armclk_parent(uint32_t mux)
{
	switch (mux) {
	case 0:
		return RK3308_PLL_APLL;
	case 1:
		return RK3308_PLL_VPLL0;
	case 2:
		return RK3308_PLL_VPLL1;
	}

	return 0;
}

uint32_t
rk3308_get_armclk(struct rkclock_softc *sc)
{
	uint32_t reg, mux, div_con;
	uint32_t idx;

	reg = HREAD4(sc, RK3308_CRU_CLKSEL_CON(0));
	mux = (reg & RK3308_CRU_CORE_CLK_PLL_SEL_MASK) >>
	    RK3308_CRU_CORE_CLK_PLL_SEL_SHIFT;
	div_con = (reg & RK3308_CRU_CLK_CORE_DIV_CON_MASK) >>
	    RK3308_CRU_CLK_CORE_DIV_CON_SHIFT;
	idx = rk3308_armclk_parent(mux);

	return rk3308_get_frequency(sc, &idx) / (div_con + 1);
}

int
rk3308_set_armclk(struct rkclock_softc *sc, uint32_t freq)
{
	uint32_t reg, mux;
	uint32_t old_freq, div;
	uint32_t idx;

	old_freq = rk3308_get_armclk(sc);
	if (freq == old_freq)
		return 0;

	reg = HREAD4(sc, RK3308_CRU_CLKSEL_CON(0));
	mux = (reg & RK3308_CRU_CORE_CLK_PLL_SEL_MASK) >>
	    RK3308_CRU_CORE_CLK_PLL_SEL_SHIFT;

	/* Keep the pclk_dbg clock at or below 300 MHz. */
	div = 1;
	while (freq / (div + 1) > 300000000)
		div++;
	/* and make sure we use an odd divider. */
	if ((div % 2) == 0)
		div++;

	/* When ramping up, set clock dividers first. */
	if (freq > old_freq) {
		HWRITE4(sc, RK3308_CRU_CLKSEL_CON(0),
		    RK3308_CRU_CLK_CORE_DIV_CON_MASK << 16 |
		    0 << RK3308_CRU_CLK_CORE_DIV_CON_SHIFT |
		    RK3308_CRU_ACLK_CORE_DIV_CON_MASK << 16 |
		    1 << RK3308_CRU_ACLK_CORE_DIV_CON_SHIFT |
		    RK3308_CRU_CLK_CORE_DBG_DIV_CON_MASK << 16 |
		    div << RK3308_CRU_CLK_CORE_DBG_DIV_CON_SHIFT);
	}

	/* We always use VPLL1 and force the switch below if needed. */
	idx = RK3308_PLL_VPLL1;
	rk3308_set_frequency(sc, &idx, freq);

	/* When ramping down, set clock dividers last. */
	if (freq < old_freq || mux != 2) {
		HWRITE4(sc, RK3308_CRU_CLKSEL_CON(0),
		    RK3308_CRU_CORE_CLK_PLL_SEL_MASK << 16 |
		    2 << RK3308_CRU_CORE_CLK_PLL_SEL_SHIFT |
		    RK3308_CRU_CLK_CORE_DIV_CON_MASK << 16 |
		    0 << RK3308_CRU_CLK_CORE_DIV_CON_SHIFT |
		    RK3308_CRU_ACLK_CORE_DIV_CON_MASK << 16 |
		    1 << RK3308_CRU_ACLK_CORE_DIV_CON_SHIFT |
		    RK3308_CRU_CLK_CORE_DBG_DIV_CON_MASK << 16 |
		    div << RK3308_CRU_CLK_CORE_DBG_DIV_CON_SHIFT);
	}

	return 0;
}

uint32_t
rk3308_get_pll(struct rkclock_softc *sc, bus_size_t base)
{
	uint32_t fbdiv, postdiv1, postdiv2, refdiv;
	uint32_t dsmpd, fracdiv;
	uint64_t frac = 0;
	uint32_t reg;

	reg = HREAD4(sc, base + 0x0000);
	postdiv1 = (reg & RK3308_CRU_PLL_POSTDIV1_MASK) >>
	    RK3308_CRU_PLL_POSTDIV1_SHIFT;
	fbdiv = (reg & RK3308_CRU_PLL_FBDIV_MASK) >>
	    RK3308_CRU_PLL_FBDIV_SHIFT;
	reg = HREAD4(sc, base + 0x0004);
	dsmpd = (reg & RK3308_CRU_PLL_DSMPD);
	postdiv2 = (reg & RK3308_CRU_PLL_POSTDIV2_MASK) >>
	    RK3308_CRU_PLL_POSTDIV2_SHIFT;
	refdiv = (reg & RK3308_CRU_PLL_REFDIV_MASK) >>
	    RK3308_CRU_PLL_REFDIV_SHIFT;
	reg = HREAD4(sc, base + 0x0008);
	fracdiv = (reg & RK3308_CRU_PLL_FRACDIV_MASK) >>
	    RK3308_CRU_PLL_FRACDIV_SHIFT;

	if (dsmpd == 0)
		frac = (24000000ULL * fracdiv / refdiv) >> 24;
	return ((24000000ULL * fbdiv / refdiv) + frac) / postdiv1 / postdiv2;
}

int
rk3308_set_pll(struct rkclock_softc *sc, bus_size_t base, uint32_t freq)
{
	uint32_t fbdiv, postdiv1, postdiv2, refdiv;
	int mode_shift = -1;

	switch (base) {
	case RK3308_CRU_APLL_CON(0):
		mode_shift = 0;
		break;
	case RK3308_CRU_DPLL_CON(0):
		mode_shift = 2;
		break;
	case RK3308_CRU_VPLL0_CON(0):
		mode_shift = 4;
		break;
	case RK3308_CRU_VPLL1_CON(0):
		mode_shift = 6;
		break;
	}
	KASSERT(mode_shift != -1);

	/*
	 * It is not clear whether all combinations of the clock
	 * dividers result in a stable clock.  Therefore this function
	 * only supports a limited set of PLL clock rates.  For now
	 * this set covers all the CPU frequencies supported by the
	 * Linux kernel.
	 */
	switch (freq) {
	case 1608000000U:
	case 1584000000U:
	case 1560000000U:
	case 1536000000U:
	case 1512000000U:
	case 1488000000U:
	case 1464000000U:
	case 1440000000U:
	case 1416000000U:
	case 1392000000U:
	case 1368000000U:
	case 1344000000U:
	case 1320000000U:
	case 1296000000U:
	case 1272000000U:
	case 1248000000U:
	case 1200000000U:
	case 1104000000U:
		postdiv1 = postdiv2 = refdiv = 1;
		break;
	case 1188000000U:
		refdiv = 2; postdiv1 = postdiv2 = 1;
		break;
	case 1100000000U:
		refdiv = 12; postdiv1 = postdiv2 = 1;
		break;
	case 1000000000U:
		refdiv = 6; postdiv1 = postdiv2 = 1;
		break;
	case 1008000000U:
	case 984000000U:
	case 960000000U:
	case 936000000U:
	case 912000000U:
	case 888000000U:
	case 864000000U:
	case 840000000U:
	case 816000000U:
	case 696000000U:
	case 624000000U:
		postdiv1 = 2; postdiv2 = refdiv = 1;
		break;
	case 900000000U:
		refdiv = 4; postdiv1 = 2; postdiv2 = 1;
		break;
	case 800000000U:
	case 700000000U:
	case 500000000U:
		refdiv = 6; postdiv1 = 2; postdiv2 = 1;
		break;
	case 600000000U:
	case 504000000U:
		postdiv1 = 3; postdiv2 = refdiv = 1;
		break;
	case 594000000U:
		refdiv = 2; postdiv1 = 2; postdiv2 = 1;
		break;
	case 408000000U:
	case 312000000U:
		postdiv1 = postdiv2 = 2; refdiv = 1;
		break;
	case 216000000U:
		postdiv1 = 4; postdiv2 = 2; refdiv = 1;
		break;
	case 96000000U:
		postdiv1 = postdiv2 = 4; refdiv = 1;
		break;
	default:
		printf("%s: %u Hz\n", __func__, freq);
		return -1;
	}

	/* Calculate feedback divider. */
	fbdiv = freq * postdiv1 * postdiv2 * refdiv / 24000000;

	/*
	 * Select slow mode to guarantee a stable clock while we're
	 * adjusting the PLL.
	 */
	HWRITE4(sc, RK3308_CRU_CRU_MODE,
	   (RK3308_CRU_CRU_MODE_MASK << 16 |
	   RK3308_CRU_CRU_MODE_SLOW) << mode_shift);

	/* Set PLL rate. */
	HWRITE4(sc, base + 0x0000,
	    RK3308_CRU_PLL_POSTDIV1_MASK << 16 |
	    postdiv1 << RK3308_CRU_PLL_POSTDIV1_SHIFT |
	    RK3308_CRU_PLL_FBDIV_MASK << 16 |
	    fbdiv << RK3308_CRU_PLL_FBDIV_SHIFT);
	HWRITE4(sc, base + 0x0004,
	    RK3308_CRU_PLL_DSMPD << 16 | RK3308_CRU_PLL_DSMPD |
	    RK3308_CRU_PLL_POSTDIV2_MASK << 16 |
	    postdiv2 << RK3308_CRU_PLL_POSTDIV2_SHIFT |
	    RK3308_CRU_PLL_REFDIV_MASK << 16 |
	    refdiv << RK3308_CRU_PLL_REFDIV_SHIFT);

	/* Wait for PLL to stabilize. */
	while ((HREAD4(sc, base + 0x0004) & RK3308_CRU_PLL_PLL_LOCK) == 0)
		delay(10);

	/* Switch back to normal mode. */
	HWRITE4(sc, RK3308_CRU_CRU_MODE,
	   (RK3308_CRU_CRU_MODE_MASK << 16 |
	   RK3308_CRU_CRU_MODE_NORMAL) << mode_shift);

	return 0;
}

uint32_t
rk3308_get_rtc32k(struct rkclock_softc *sc)
{
	uint32_t reg, mux, pll, div_con;

	reg = HREAD4(sc, RK3308_CRU_CLKSEL_CON(2));
	mux = (reg & 0x300) >> 8;
	if (mux != 3) {
		printf("%s: RTC32K not using clk_32k_div\n", __func__);
		return 0;
	}

	if ((reg >> 10) & 1)
		pll = RK3308_PLL_VPLL1;
	else
		pll = RK3308_PLL_VPLL0;

	div_con = HREAD4(sc, RK3308_CRU_CLKSEL_CON(4)) & 0xffff;
	return rk3308_get_frequency(sc, &pll) / (div_con + 1);
}

int
rk3308_set_rtc32k(struct rkclock_softc *sc, uint32_t freq)
{
	const struct rkclock *clk;
	uint32_t vpll0_freq, vpll1_freq, mux, div_con;

	clk = rkclock_lookup(sc, RK3308_CLK_RTC32K);
	vpll0_freq = rkclock_freq(sc, clk, 0, freq);
	vpll1_freq = rkclock_freq(sc, clk, 1, freq);
	mux = 0;
	freq = vpll0_freq;

	if ((vpll1_freq > vpll0_freq && vpll1_freq <= freq) ||
	    (vpll1_freq < vpll0_freq && vpll1_freq >= freq)) {
		mux = 1;
		freq = vpll1_freq;
	}

	div_con = rkclock_div_con(sc, clk, mux, freq);
	HWRITE4(sc, RK3308_CRU_CLKSEL_CON(2), 1 << 26 | (mux << 10));
	HWRITE4(sc, RK3308_CRU_CLKSEL_CON(4), 0xffff0000 | div_con);
	return 0;
}

uint32_t
rk3308_get_frequency(void *cookie, uint32_t *cells)
{
	struct rkclock_softc *sc = cookie;
	uint32_t idx = cells[0];

	switch (idx) {
	case RK3308_PLL_APLL:
		return rk3308_get_pll(sc, RK3308_CRU_APLL_CON(0));
	case RK3308_PLL_DPLL:
		return rk3308_get_pll(sc, RK3308_CRU_DPLL_CON(0));
	case RK3308_PLL_VPLL0:
		return rk3308_get_pll(sc, RK3308_CRU_VPLL0_CON(0));
	case RK3308_PLL_VPLL1:
		return rk3308_get_pll(sc, RK3308_CRU_VPLL1_CON(0));
	case RK3308_ARMCLK:
		return rk3308_get_armclk(sc);
	case RK3308_XIN24M:
		return 24000000;
	case RK3308_CLK_RTC32K:
		return rk3308_get_rtc32k(sc);

	/*
	 * XXX The USB480M clock is external.  Returning zero here will cause
	 * it to be ignored for reparenting purposes.
	 */
	case RK3308_USB480M:
		return 0;
	default:
		break;
	}

	return rkclock_get_frequency(sc, idx);
}

int
rk3308_set_frequency(void *cookie, uint32_t *cells, uint32_t freq)
{
	struct rkclock_softc *sc = cookie;
	uint32_t idx = cells[0];

	switch (idx) {
	case RK3308_PLL_APLL:
		return rk3308_set_pll(sc, RK3308_CRU_APLL_CON(0), freq);
	case RK3308_PLL_DPLL:
		return rk3308_set_pll(sc, RK3308_CRU_DPLL_CON(0), freq);
	case RK3308_PLL_VPLL0:
		return rk3308_set_pll(sc, RK3308_CRU_VPLL0_CON(0), freq);
	case RK3308_PLL_VPLL1:
		return rk3308_set_pll(sc, RK3308_CRU_VPLL1_CON(0), freq);
	case RK3308_ARMCLK:
		return rk3308_set_armclk(sc, freq);
	case RK3308_CLK_RTC32K:
		return rk3308_set_rtc32k(sc, freq);
	default:
		break;
	}

	return rkclock_set_frequency(sc, idx, freq);
}


int
rk3308_set_parent(void *cookie, uint32_t *cells, uint32_t *pcells)
{
	struct rkclock_softc *sc = cookie;

	if (pcells[0] != sc->sc_phandle)
		return -1;

	return rkclock_set_parent(sc, cells[0], pcells[1]);
}

void
rk3308_enable(void *cookie, uint32_t *cells, int on)
{
	uint32_t idx = cells[0];

	/*
	 * All clocks are enabled by default, so there is nothing for
	 * us to do until we start disabling clocks.
	 */
	if (!on)
		printf("%s: 0x%08x\n", __func__, idx);
}

void
rk3308_reset(void *cookie, uint32_t *cells, int on)
{
	struct rkclock_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t mask = (1 << (idx % 16));

	HWRITE4(sc, RK3308_CRU_SOFTRST_CON(idx / 16),
	    mask << 16 | (on ? mask : 0));
}


/*
 * Rockchip RK3328
 */

const struct rkclock rk3328_clocks[] = {
	{
		RK3328_CLK_RTC32K, RK3328_CRU_CLKSEL_CON(38),
		SEL(15, 14), DIV(13, 0),
		{ RK3328_PLL_CPLL, RK3328_PLL_GPLL, RK3328_XIN24M }
	},
	{
		RK3328_CLK_SPI, RK3328_CRU_CLKSEL_CON(24),
		SEL(7, 7), DIV(6, 0),
		{ RK3328_PLL_CPLL, RK3328_PLL_GPLL }
	},
	{
		RK3328_CLK_SDMMC, RK3328_CRU_CLKSEL_CON(30),
		SEL(9, 8), DIV(7, 0),
		{ RK3328_PLL_CPLL, RK3328_PLL_GPLL, RK3328_XIN24M,
		  RK3328_USB480M }
	},
	{
		RK3328_CLK_SDIO, RK3328_CRU_CLKSEL_CON(31),
		SEL(9, 8), DIV(7, 0),
		{ RK3328_PLL_CPLL, RK3328_PLL_GPLL, RK3328_XIN24M,
		  RK3328_USB480M }
	},
	{
		RK3328_CLK_EMMC, RK3328_CRU_CLKSEL_CON(32),
		SEL(9, 8), DIV(7, 0),
		{ RK3328_PLL_CPLL, RK3328_PLL_GPLL, RK3328_XIN24M,
		  RK3328_USB480M }
	},
	{
		RK3328_CLK_TSADC, RK3328_CRU_CLKSEL_CON(22),
		0, DIV(9, 0),
		{ RK3328_CLK_24M }
	},
	{
		RK3328_CLK_UART0, RK3328_CRU_CLKSEL_CON(14),
		SEL(9, 8), 0,
		{ 0, 0, RK3328_XIN24M, RK3328_XIN24M }
	},
	{
		RK3328_CLK_UART1, RK3328_CRU_CLKSEL_CON(16),
		SEL(9, 8), 0,
		{ 0, 0, RK3328_XIN24M, RK3328_XIN24M }
	},
	{
		RK3328_CLK_UART2, RK3328_CRU_CLKSEL_CON(18),
		SEL(9, 8), 0,
		{ 0, 0, RK3328_XIN24M, RK3328_XIN24M }
	},
	{
		RK3328_CLK_WIFI, RK3328_CRU_CLKSEL_CON(52),
		SEL(7, 6), DIV(5, 0),
		{ RK3328_PLL_CPLL, RK3328_PLL_GPLL, RK3328_USB480M }
	},
	{
		RK3328_CLK_I2C0, RK3328_CRU_CLKSEL_CON(34),
		SEL(7, 7), DIV(6, 0),
		{ RK3328_PLL_CPLL, RK3328_PLL_GPLL }
	},
	{
		RK3328_CLK_I2C1, RK3328_CRU_CLKSEL_CON(34),
		SEL(15, 15), DIV(14, 8),
		{ RK3328_PLL_CPLL, RK3328_PLL_GPLL }
	},
	{
		RK3328_CLK_I2C2, RK3328_CRU_CLKSEL_CON(35),
		SEL(7, 7), DIV(6, 0),
		{ RK3328_PLL_CPLL, RK3328_PLL_GPLL }
	},
	{
		RK3328_CLK_I2C3, RK3328_CRU_CLKSEL_CON(35),
		SEL(15, 15), DIV(14, 8),
		{ RK3328_PLL_CPLL, RK3328_PLL_GPLL }
	},
	{
		RK3328_CLK_CRYPTO, RK3328_CRU_CLKSEL_CON(20),
		SEL(7, 7), DIV(4, 0),
		{ RK3328_PLL_CPLL, RK3328_PLL_GPLL }
	},
	{
		RK3328_CLK_PDM, RK3328_CRU_CLKSEL_CON(20),
		SEL(15, 14), DIV(12, 8),
		{ RK3328_PLL_CPLL, RK3328_PLL_GPLL, RK3328_PLL_APLL },
		FIXED_PARENT | SET_PARENT
	},
	{
		RK3328_CLK_VDEC_CABAC, RK3328_CRU_CLKSEL_CON(48),
		SEL(15, 14), DIV(12, 8),
		{ RK3328_PLL_CPLL, RK3328_PLL_GPLL, RK3328_HDMIPHY,
		  RK3328_USB480M }
	},
	{
		RK3328_CLK_VDEC_CORE, RK3328_CRU_CLKSEL_CON(49),
		SEL(7, 6), DIV(4, 0),
		{ RK3328_PLL_CPLL, RK3328_PLL_GPLL, RK3328_HDMIPHY,
		  RK3328_USB480M }
	},
	{
		RK3328_CLK_VENC_DSP, RK3328_CRU_CLKSEL_CON(52),
		SEL(15, 14), DIV(12, 8),
		{ RK3328_PLL_CPLL, RK3328_PLL_GPLL, RK3328_HDMIPHY,
		  RK3328_USB480M }
	},
	{
		RK3328_CLK_VENC_CORE, RK3328_CRU_CLKSEL_CON(51),
		SEL(15, 14), DIV(12, 8),
		{ RK3328_PLL_CPLL, RK3328_PLL_GPLL, RK3328_HDMIPHY,
		  RK3328_USB480M }
	},
	{
		RK3328_CLK_TSP, RK3328_CRU_CLKSEL_CON(21),
		SEL(15, 15), DIV(12, 8),
		{ RK3328_PLL_CPLL, RK3328_PLL_GPLL }
	},
	{
		RK3328_CLK_MAC2IO_SRC, RK3328_CRU_CLKSEL_CON(27),
		SEL(7, 7), DIV(4, 0),
		{ RK3328_PLL_CPLL, RK3328_PLL_GPLL }
	},
	{
		RK3328_DCLK_LCDC, RK3328_CRU_CLKSEL_CON(40),
		SEL(1, 1), 0,
		{ RK3328_HDMIPHY, RK3328_DCLK_LCDC_SRC }
	},
	{
		RK3328_ACLK_VOP_PRE, RK3328_CRU_CLKSEL_CON(39),
		SEL(7, 6), DIV(4, 0),
		{ RK3328_PLL_CPLL, RK3328_PLL_GPLL, RK3328_HDMIPHY,
		  RK3328_USB480M }
	},
	{
		RK3328_ACLK_RGA_PRE, RK3328_CRU_CLKSEL_CON(36),
		SEL(15, 14), DIV(12, 8),
		{ RK3328_PLL_CPLL, RK3328_PLL_GPLL, RK3328_HDMIPHY,
		  RK3328_USB480M }
	},
	{
		RK3328_ACLK_BUS_PRE, RK3328_CRU_CLKSEL_CON(0),
		SEL(14, 13), DIV(12, 8),
		{ RK3328_PLL_CPLL, RK3328_PLL_GPLL, RK3328_HDMIPHY }
	},
	{
		RK3328_ACLK_PERI_PRE, RK3328_CRU_CLKSEL_CON(28),
		SEL(7, 6), DIV(4, 0),
		{ RK3328_PLL_CPLL, RK3328_PLL_GPLL, RK3328_HDMIPHY }
	},
	{
		RK3328_ACLK_RKVDEC_PRE, RK3328_CRU_CLKSEL_CON(48),
		SEL(7, 6), DIV(4, 0),
		{ RK3328_PLL_CPLL, RK3328_PLL_GPLL, RK3328_HDMIPHY,
		  RK3328_USB480M }
	},
	{
		RK3328_ACLK_RKVENC, RK3328_CRU_CLKSEL_CON(51),
		SEL(7, 6), DIV(4, 0),
		{ RK3328_PLL_CPLL, RK3328_PLL_GPLL, RK3328_HDMIPHY,
		  RK3328_USB480M }
	},
	{
		RK3328_ACLK_VPU_PRE, RK3328_CRU_CLKSEL_CON(50),
		SEL(7, 6), DIV(4, 0),
		{ RK3328_PLL_CPLL, RK3328_PLL_GPLL, RK3328_HDMIPHY,
		  RK3328_USB480M }
	},
	{
		RK3328_ACLK_VIO_PRE, RK3328_CRU_CLKSEL_CON(37),
		SEL(7, 6), DIV(4, 0),
		{ RK3328_PLL_CPLL, RK3328_PLL_GPLL, RK3328_HDMIPHY,
		  RK3328_USB480M }
	},
	{
		RK3328_PCLK_BUS_PRE, RK3328_CRU_CLKSEL_CON(1),
		0, DIV(14, 12),
		{ RK3328_ACLK_BUS_PRE }
	},
	{
		RK3328_HCLK_BUS_PRE, RK3328_CRU_CLKSEL_CON(1),
		0, DIV(9, 8),
		{ RK3328_ACLK_BUS_PRE }
	},
	{
		RK3328_PCLK_PERI, RK3328_CRU_CLKSEL_CON(29),
		0, DIV(6, 4),
		{ RK3328_ACLK_PERI_PRE }
	},
	{
		RK3328_HCLK_PERI, RK3328_CRU_CLKSEL_CON(29),
		0, DIV(1, 0),
		{ RK3328_ACLK_PERI_PRE }
	},
	{
		RK3328_CLK_24M, RK3328_CRU_CLKSEL_CON(2),
		0, DIV(12, 8),
		{ RK3328_XIN24M }
	},
	{
		/* Sentinel */
	}
};

void
rk3328_init(struct rkclock_softc *sc)
{
	int i;

	/* The code below assumes all clocks are enabled.  Check this!. */
	for (i = 0; i <= 28; i++) {
		if (HREAD4(sc, RK3328_CRU_CLKGATE_CON(i)) != 0x00000000) {
			printf("CRU_CLKGATE_CON%d: 0x%08x\n", i,
			    HREAD4(sc, RK3328_CRU_CLKGATE_CON(i)));
		}
	}

	sc->sc_clocks = rk3328_clocks;
}

uint32_t
rk3328_armclk_parent(uint32_t mux)
{
	switch (mux) {
	case 0:
		return RK3328_PLL_APLL;
	case 1:
		return RK3328_PLL_GPLL;
	case 2:
		return RK3328_PLL_DPLL;
	case 3:
		return RK3328_PLL_NPLL;
	}

	return 0;
}

uint32_t
rk3328_get_armclk(struct rkclock_softc *sc)
{
	uint32_t reg, mux, div_con;
	uint32_t idx;

	reg = HREAD4(sc, RK3328_CRU_CLKSEL_CON(0));
	mux = (reg & RK3328_CRU_CORE_CLK_PLL_SEL_MASK) >>
	    RK3328_CRU_CORE_CLK_PLL_SEL_SHIFT;
	div_con = (reg & RK3328_CRU_CLK_CORE_DIV_CON_MASK) >>
	    RK3328_CRU_CLK_CORE_DIV_CON_SHIFT;
	idx = rk3328_armclk_parent(mux);

	return rk3328_get_frequency(sc, &idx) / (div_con + 1);
}

int
rk3328_set_armclk(struct rkclock_softc *sc, uint32_t freq)
{
	uint32_t reg, mux;
	uint32_t old_freq, div;
	uint32_t idx;

	old_freq = rk3328_get_armclk(sc);
	if (freq == old_freq)
		return 0;

	reg = HREAD4(sc, RK3328_CRU_CLKSEL_CON(0));
	mux = (reg & RK3328_CRU_CORE_CLK_PLL_SEL_MASK) >>
	    RK3328_CRU_CORE_CLK_PLL_SEL_SHIFT;

	/* Keep the pclk_dbg clock at or below 300 MHz. */
	div = 1;
	while (freq / (div + 1) > 300000000)
		div++;
	/* and make sure we use an odd divider. */
	if ((div % 2) == 0)
		div++;

	/* When ramping up, set clock dividers first. */
	if (freq > old_freq) {
		HWRITE4(sc, RK3328_CRU_CLKSEL_CON(0),
		    RK3328_CRU_CLK_CORE_DIV_CON_MASK << 16 |
		    0 << RK3328_CRU_CLK_CORE_DIV_CON_SHIFT);
		HWRITE4(sc, RK3328_CRU_CLKSEL_CON(1),
		    RK3328_CRU_ACLK_CORE_DIV_CON_MASK << 16 |
		    1 << RK3328_CRU_ACLK_CORE_DIV_CON_SHIFT |
		    RK3328_CRU_CLK_CORE_DBG_DIV_CON_MASK << 16 |
		    div << RK3328_CRU_CLK_CORE_DBG_DIV_CON_SHIFT);
	}

	/* We always use NPLL and force the switch below if needed. */
	idx = RK3328_PLL_NPLL;
	rk3328_set_frequency(sc, &idx, freq);

	/* When ramping down, set clock dividers last. */
	if (freq < old_freq || mux != 3) {
		HWRITE4(sc, RK3328_CRU_CLKSEL_CON(0),
		    RK3328_CRU_CORE_CLK_PLL_SEL_MASK << 16 |
		    3 << RK3328_CRU_CORE_CLK_PLL_SEL_SHIFT |
		    RK3328_CRU_CLK_CORE_DIV_CON_MASK << 16 |
		    0 << RK3328_CRU_CLK_CORE_DIV_CON_SHIFT);
		HWRITE4(sc, RK3328_CRU_CLKSEL_CON(1),
		    RK3328_CRU_ACLK_CORE_DIV_CON_MASK << 16 |
		    1 << RK3328_CRU_ACLK_CORE_DIV_CON_SHIFT |
		    RK3328_CRU_CLK_CORE_DBG_DIV_CON_MASK << 16 |
		    div << RK3328_CRU_CLK_CORE_DBG_DIV_CON_SHIFT);
	}

	return 0;
}

uint32_t
rk3328_get_pll(struct rkclock_softc *sc, bus_size_t base)
{
	uint32_t fbdiv, postdiv1, postdiv2, refdiv;
	uint32_t dsmpd, fracdiv;
	uint64_t frac = 0;
	uint32_t reg;

	reg = HREAD4(sc, base + 0x0000);
	postdiv1 = (reg & RK3328_CRU_PLL_POSTDIV1_MASK) >>
	    RK3328_CRU_PLL_POSTDIV1_SHIFT;
	fbdiv = (reg & RK3328_CRU_PLL_FBDIV_MASK) >>
	    RK3328_CRU_PLL_FBDIV_SHIFT;
	reg = HREAD4(sc, base + 0x0004);
	dsmpd = (reg & RK3328_CRU_PLL_DSMPD);
	postdiv2 = (reg & RK3328_CRU_PLL_POSTDIV2_MASK) >>
	    RK3328_CRU_PLL_POSTDIV2_SHIFT;
	refdiv = (reg & RK3328_CRU_PLL_REFDIV_MASK) >>
	    RK3328_CRU_PLL_REFDIV_SHIFT;
	reg = HREAD4(sc, base + 0x0008);
	fracdiv = (reg & RK3328_CRU_PLL_FRACDIV_MASK) >>
	    RK3328_CRU_PLL_FRACDIV_SHIFT;

	if (dsmpd == 0)
		frac = (24000000ULL * fracdiv / refdiv) >> 24;
	return ((24000000ULL * fbdiv / refdiv) + frac) / postdiv1 / postdiv2;
}

int
rk3328_set_pll(struct rkclock_softc *sc, bus_size_t base, uint32_t freq)
{
	uint32_t fbdiv, postdiv1, postdiv2, refdiv;
	int mode_shift = -1;

	switch (base) {
	case RK3328_CRU_APLL_CON(0):
		mode_shift = 0;
		break;
	case RK3328_CRU_DPLL_CON(0):
		mode_shift = 4;
		break;
	case RK3328_CRU_CPLL_CON(0):
		mode_shift = 8;
		break;
	case RK3328_CRU_GPLL_CON(0):
		mode_shift = 12;
		break;
	case RK3328_CRU_NPLL_CON(0):
		mode_shift = 1;
		break;
	}
	KASSERT(mode_shift != -1);

	/*
	 * It is not clear whether all combinations of the clock
	 * dividers result in a stable clock.  Therefore this function
	 * only supports a limited set of PLL clock rates.  For now
	 * this set covers all the CPU frequencies supported by the
	 * Linux kernel.
	 */
	switch (freq) {
	case 1800000000U:
	case 1704000000U:
	case 1608000000U:
	case 1512000000U:
	case 1488000000U:
	case 1416000000U:
	case 1392000000U:
	case 1296000000U:
	case 1200000000U:
	case 1104000000U:
		postdiv1 = postdiv2 = refdiv = 1;
		break;
	case 1008000000U:
	case 912000000U:
	case 816000000U:
	case 696000000U:
		postdiv1 = 2; postdiv2 = refdiv = 1;
		break;
	case 600000000U:
		postdiv1 = 3; postdiv2 = refdiv = 1;
		break;
	case 408000000U:
	case 312000000U:
		postdiv1 = postdiv2 = 2; refdiv = 1;
		break;
	case 216000000U:
		postdiv1 = 4; postdiv2 = 2; refdiv = 1;
		break;
	case 96000000U:
		postdiv1 = postdiv2 = 4; refdiv = 1;
		break;
	default:
		printf("%s: %u Hz\n", __func__, freq);
		return -1;
	}

	/* Calculate feedback divider. */
	fbdiv = freq * postdiv1 * postdiv2 * refdiv / 24000000;

	/*
	 * Select slow mode to guarantee a stable clock while we're
	 * adjusting the PLL.
	 */
	HWRITE4(sc, RK3328_CRU_CRU_MODE,
	   (RK3328_CRU_CRU_MODE_MASK << 16 |
	   RK3328_CRU_CRU_MODE_SLOW) << mode_shift);

	/* Set PLL rate. */
	HWRITE4(sc, base + 0x0000,
	    RK3328_CRU_PLL_POSTDIV1_MASK << 16 |
	    postdiv1 << RK3328_CRU_PLL_POSTDIV1_SHIFT |
	    RK3328_CRU_PLL_FBDIV_MASK << 16 |
	    fbdiv << RK3328_CRU_PLL_FBDIV_SHIFT);
	HWRITE4(sc, base + 0x0004,
	    RK3328_CRU_PLL_DSMPD << 16 | RK3328_CRU_PLL_DSMPD |
	    RK3328_CRU_PLL_POSTDIV2_MASK << 16 |
	    postdiv2 << RK3328_CRU_PLL_POSTDIV2_SHIFT |
	    RK3328_CRU_PLL_REFDIV_MASK << 16 |
	    refdiv << RK3328_CRU_PLL_REFDIV_SHIFT);

	/* Wait for PLL to stabilize. */
	while ((HREAD4(sc, base + 0x0004) & RK3328_CRU_PLL_PLL_LOCK) == 0)
		delay(10);

	/* Switch back to normal mode. */
	HWRITE4(sc, RK3328_CRU_CRU_MODE,
	   (RK3328_CRU_CRU_MODE_MASK << 16 |
	   RK3328_CRU_CRU_MODE_NORMAL) << mode_shift);

	return 0;
}

int
rk3328_set_frac_pll(struct rkclock_softc *sc, bus_size_t base, uint32_t freq)
{
	uint32_t fbdiv, postdiv1, postdiv2, refdiv, fracdiv;
	int mode_shift = -1;
	uint32_t reg;

	switch (base) {
	case RK3328_CRU_APLL_CON(0):
		mode_shift = 0;
		break;
	case RK3328_CRU_DPLL_CON(0):
		mode_shift = 4;
		break;
	case RK3328_CRU_CPLL_CON(0):
		mode_shift = 8;
		break;
	case RK3328_CRU_GPLL_CON(0):
		mode_shift = 12;
		break;
	case RK3328_CRU_NPLL_CON(0):
		mode_shift = 1;
		break;
	}
	KASSERT(mode_shift != -1);

	/*
	 * It is not clear whether all combinations of the clock
	 * dividers result in a stable clock.  Therefore this function
	 * only supports a limited set of PLL clock rates.  This set
	 * set covers all the fractional PLL frequencies supported by
	 * the Linux kernel.
	 */
	switch (freq) {
	case 1016064000U:
		postdiv1 = postdiv2 = 1; refdiv = 3; fracdiv = 134217;
		break;
	case 983040000U:
		postdiv1 = postdiv2 = 1; refdiv = 24; fracdiv = 671088;
		break;
	case 491520000U:
		postdiv1 = 2; postdiv2 = 1; refdiv = 24; fracdiv = 671088;
		break;
	case 61440000U:
		postdiv1 = 7; postdiv2 = 2; refdiv = 6; fracdiv = 671088;
		break;
	case 56448000U:
		postdiv1 = postdiv2 = 4; refdiv = 12; fracdiv = 9797894;
		break;
	case 40960000U:
		postdiv1 = 4; postdiv2 = 5; refdiv = 12; fracdiv = 10066239;
		break;
	default:
		printf("%s: %u Hz\n", __func__, freq);
		return -1;
	}

	/* Calculate feedback divider. */
	fbdiv = (uint64_t)freq * postdiv1 * postdiv2 * refdiv / 24000000;

	/*
	 * Select slow mode to guarantee a stable clock while we're
	 * adjusting the PLL.
	 */
	HWRITE4(sc, RK3328_CRU_CRU_MODE,
	   (RK3328_CRU_CRU_MODE_MASK << 16 |
	   RK3328_CRU_CRU_MODE_SLOW) << mode_shift);

	/* Set PLL rate. */
	HWRITE4(sc, base + 0x0000,
	    RK3328_CRU_PLL_POSTDIV1_MASK << 16 |
	    postdiv1 << RK3328_CRU_PLL_POSTDIV1_SHIFT |
	    RK3328_CRU_PLL_FBDIV_MASK << 16 |
	    fbdiv << RK3328_CRU_PLL_FBDIV_SHIFT);
	HWRITE4(sc, base + 0x0004,
	    RK3328_CRU_PLL_DSMPD << 16 |
	    RK3328_CRU_PLL_POSTDIV2_MASK << 16 |
	    postdiv2 << RK3328_CRU_PLL_POSTDIV2_SHIFT |
	    RK3328_CRU_PLL_REFDIV_MASK << 16 |
	    refdiv << RK3328_CRU_PLL_REFDIV_SHIFT);
	reg = HREAD4(sc, base + 0x0008);
	reg &= ~RK3328_CRU_PLL_FRACDIV_MASK;
	reg |= fracdiv << RK3328_CRU_PLL_FRACDIV_SHIFT;
	HWRITE4(sc, base + 0x0008, reg);

	/* Wait for PLL to stabilize. */
	while ((HREAD4(sc, base + 0x0004) & RK3328_CRU_PLL_PLL_LOCK) == 0)
		delay(10);

	/* Switch back to normal mode. */
	HWRITE4(sc, RK3328_CRU_CRU_MODE,
	   (RK3328_CRU_CRU_MODE_MASK << 16 |
	   RK3328_CRU_CRU_MODE_NORMAL) << mode_shift);

	return 0;
}

uint32_t
rk3328_get_frequency(void *cookie, uint32_t *cells)
{
	struct rkclock_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t reg;

	switch (idx) {
	case RK3328_PLL_APLL:
		return rk3328_get_pll(sc, RK3328_CRU_APLL_CON(0));
	case RK3328_PLL_DPLL:
		return rk3328_get_pll(sc, RK3328_CRU_DPLL_CON(0));
	case RK3328_PLL_CPLL:
		return rk3328_get_pll(sc, RK3328_CRU_CPLL_CON(0));
	case RK3328_PLL_GPLL:
		return rk3328_get_pll(sc, RK3328_CRU_GPLL_CON(0));
	case RK3328_PLL_NPLL:
		return rk3328_get_pll(sc, RK3328_CRU_NPLL_CON(0));
	case RK3328_ARMCLK:
		return rk3328_get_armclk(sc);
	case RK3328_XIN24M:
		return 24000000;
	case RK3328_GMAC_CLKIN:
		return 125000000;
	/*
	 * XXX The HDMIPHY and USB480M clocks are external.  Returning
	 * zero here will cause them to be ignored for reparenting
	 * purposes.
	 */
	case RK3328_HDMIPHY:
		return 0;
	case RK3328_USB480M:
		return 0;
	case RK3328_CLK_MAC2IO:
		reg = regmap_read_4(sc->sc_grf, RK3328_GRF_MAC_CON1);
		if (reg & RK3328_GRF_GMAC2IO_RMII_EXTCLK_SEL)
			idx = RK3328_GMAC_CLKIN;
		else
			idx = RK3328_CLK_MAC2IO_SRC;
		return rk3328_get_frequency(sc, &idx);
	default:
		break;
	}

	return rkclock_get_frequency(sc, idx);
}

int
rk3328_set_frequency(void *cookie, uint32_t *cells, uint32_t freq)
{
	struct rkclock_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t reg, mux;

	switch (idx) {
	case RK3328_PLL_APLL:
		return rk3328_set_frac_pll(sc, RK3328_CRU_APLL_CON(0), freq);
	case RK3328_PLL_DPLL:
		return rk3328_set_pll(sc, RK3328_CRU_DPLL_CON(0), freq);
	case RK3328_PLL_CPLL:
		return rk3328_set_pll(sc, RK3328_CRU_CPLL_CON(0), freq);
	case RK3328_PLL_GPLL:
		return rk3328_set_frac_pll(sc, RK3328_CRU_GPLL_CON(0), freq);
	case RK3328_PLL_NPLL:
		return rk3328_set_pll(sc, RK3328_CRU_NPLL_CON(0), freq);
	case RK3328_ARMCLK:
		return rk3328_set_armclk(sc, freq);
	case RK3328_CLK_UART0:
	case RK3328_CLK_UART1:
	case RK3328_CLK_UART2:
		if (freq == rk3328_get_frequency(sc, &idx))
			return 0;
		break;
	case RK3328_DCLK_LCDC:
		reg = HREAD4(sc, RK3328_CRU_CLKSEL_CON(40));
		mux = (reg & RK3328_CRU_VOP_DCLK_SRC_SEL_MASK) >>
		    RK3328_CRU_VOP_DCLK_SRC_SEL_SHIFT;
		idx = (mux == 0) ? RK3328_HDMIPHY : RK3328_DCLK_LCDC_SRC;
		return rk3328_set_frequency(sc, &idx, freq);
	case RK3328_HCLK_CRYPTO_SLV:
		idx = RK3328_HCLK_BUS_PRE;
		return rk3328_set_frequency(sc, &idx, freq);
	default:
		break;
	}

	return rkclock_set_frequency(sc, idx, freq);
}

int
rk3328_set_parent(void *cookie, uint32_t *cells, uint32_t *pcells)
{
	struct rkclock_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t parent;

	if (pcells[0] == sc->sc_phandle)
		parent = pcells[1];
	else {
		char name[32];
		int node;

		node = OF_getnodebyphandle(pcells[0]);
		if (node == 0)
			return -1;
		name[0] = 0;
		OF_getprop(node, "clock-output-names", name, sizeof(name));
		name[sizeof(name) - 1] = 0;
		if (strcmp(name, "xin24m") == 0)
			parent = RK3328_XIN24M;
		else if (strcmp(name, "gmac_clkin") == 0)
			parent = RK3328_GMAC_CLKIN;
		else
			return -1;
	}

	switch (idx) {
	case RK3328_CLK_MAC2IO:
		if (parent == RK3328_GMAC_CLKIN) {
			regmap_write_4(sc->sc_grf, RK3328_GRF_MAC_CON1,
			    RK3328_GRF_GMAC2IO_RMII_EXTCLK_SEL << 16 |
			    RK3328_GRF_GMAC2IO_RMII_EXTCLK_SEL);
		} else {
			regmap_write_4(sc->sc_grf, RK3328_GRF_MAC_CON1,
			    RK3328_GRF_GMAC2IO_RMII_EXTCLK_SEL << 16);
		}
		return 0;
	case RK3328_CLK_MAC2IO_EXT:
		if (parent == RK3328_GMAC_CLKIN) {
			regmap_write_4(sc->sc_grf, RK3328_GRF_SOC_CON4,
			    RK3328_GRF_GMAC2IO_MAC_CLK_OUTPUT_EN << 16 |
			    RK3328_GRF_GMAC2IO_MAC_CLK_OUTPUT_EN);
		} else {
			regmap_write_4(sc->sc_grf, RK3328_GRF_SOC_CON4,
			    RK3328_GRF_GMAC2IO_MAC_CLK_OUTPUT_EN << 16);
		}
		return 0;
	}

	return rkclock_set_parent(sc, idx, parent);
}

void
rk3328_enable(void *cookie, uint32_t *cells, int on)
{
	uint32_t idx = cells[0];

	/*
	 * All clocks are enabled by default, so there is nothing for
	 * us to do until we start disabling clocks.
	 */
	if (!on)
		printf("%s: 0x%08x\n", __func__, idx);
}

void
rk3328_reset(void *cookie, uint32_t *cells, int on)
{
	struct rkclock_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t mask = (1 << (idx % 16));

	HWRITE4(sc, RK3328_CRU_SOFTRST_CON(idx / 16),
	    mask << 16 | (on ? mask : 0));
}

/* 
 * Rockchip RK3399 
 */

const struct rkclock rk3399_clocks[] = {
	{
		RK3399_CLK_I2C1, RK3399_CRU_CLKSEL_CON(61),
		SEL(7, 7), DIV(6, 0),
		{ RK3399_PLL_CPLL, RK3399_PLL_GPLL }
	},
	{
		RK3399_CLK_I2C2, RK3399_CRU_CLKSEL_CON(62),
		SEL(7, 7), DIV(6, 0),
		{ RK3399_PLL_CPLL, RK3399_PLL_GPLL }
	},
	{
		RK3399_CLK_I2C3, RK3399_CRU_CLKSEL_CON(63),
		SEL(7, 7), DIV(6, 0),
		{ RK3399_PLL_CPLL, RK3399_PLL_GPLL }
	},
	{
		RK3399_CLK_I2C5, RK3399_CRU_CLKSEL_CON(61),
		SEL(15, 15), DIV(14, 8),
		{ RK3399_PLL_CPLL, RK3399_PLL_GPLL }
	},
	{
		RK3399_CLK_I2C6, RK3399_CRU_CLKSEL_CON(62),
		SEL(15, 15), DIV(14, 8),
		{ RK3399_PLL_CPLL, RK3399_PLL_GPLL }
	},
	{
		RK3399_CLK_I2C7, RK3399_CRU_CLKSEL_CON(63),
		SEL(15, 15), DIV(14, 8),
		{ RK3399_PLL_CPLL, RK3399_PLL_GPLL }
	},
	{
		RK3399_CLK_SPI0, RK3399_CRU_CLKSEL_CON(59),
		SEL(7, 7), DIV(6, 0),
		{ RK3399_PLL_CPLL, RK3399_PLL_GPLL }
	},
	{
		RK3399_CLK_SPI1, RK3399_CRU_CLKSEL_CON(59),
		SEL(15, 15), DIV(14, 8),
		{ RK3399_PLL_CPLL, RK3399_PLL_GPLL }
	},
	{
		RK3399_CLK_SPI2, RK3399_CRU_CLKSEL_CON(60),
		SEL(7, 7), DIV(6, 0),
		{ RK3399_PLL_CPLL, RK3399_PLL_GPLL }
	},
	{
		RK3399_CLK_SPI4, RK3399_CRU_CLKSEL_CON(60),
		SEL(15, 15), DIV(14, 8),
		{ RK3399_PLL_CPLL, RK3399_PLL_GPLL }
	},
	{
		RK3399_CLK_SPI5, RK3399_CRU_CLKSEL_CON(58),
		SEL(15, 15), DIV(14, 8),
		{ RK3399_PLL_CPLL, RK3399_PLL_GPLL }
	},
	{
		RK3399_CLK_SDMMC, RK3399_CRU_CLKSEL_CON(16),
		SEL(10, 8), DIV(6, 0),
		{ RK3399_PLL_CPLL, RK3399_PLL_GPLL, RK3399_PLL_NPLL,
		  /* RK3399_PLL_PPLL */ 0, /* RK3399_USB_480M */ 0,
		  RK3399_XIN24M }
	},
	{
		RK3399_CLK_SDIO, RK3399_CRU_CLKSEL_CON(15),
		SEL(10, 8), DIV(6, 0),
		{ RK3399_PLL_CPLL, RK3399_PLL_GPLL, RK3399_PLL_NPLL,
		  /* RK3399_PLL_PPLL */ 0, /* RK3399_USB_480M */ 0,
		  RK3399_XIN24M }
	},
	{
		RK3399_CLK_EMMC, RK3399_CRU_CLKSEL_CON(22),
		SEL(10, 8), DIV(6, 0),
		{ RK3399_PLL_CPLL, RK3399_PLL_GPLL, RK3399_PLL_NPLL,
		  /* RK3399_USB_480M */ 0, RK3399_XIN24M }
	},
	{
		RK3399_CLK_TSADC, RK3399_CRU_CLKSEL_CON(27),
		SEL(15, 15), DIV(9, 0),
		{ RK3399_XIN24M, RK3399_CLK_32K }
	},
	{
		RK3399_CLK_UART0, RK3399_CRU_CLKSEL_CON(33),
		SEL(9, 8), 0,
		{ 0, 0, RK3399_XIN24M }
	},
	{
		RK3399_CLK_UART1, RK3399_CRU_CLKSEL_CON(34),
		SEL(9, 8), 0,
		{ 0, 0, RK3399_XIN24M }
	},
	{
		RK3399_CLK_UART2, RK3399_CRU_CLKSEL_CON(35),
		SEL(9, 8), 0,
		{ 0, 0, RK3399_XIN24M }
	},
	{
		RK3399_CLK_UART3, RK3399_CRU_CLKSEL_CON(36),
		SEL(9, 8), 0,
		{ 0, 0, RK3399_XIN24M }
	},
	{
		RK3399_CLK_I2S0_8CH, RK3399_CRU_CLKSEL_CON(28),
		SEL(9, 8), 0,
		{ RK3399_CLK_I2S0_DIV, RK3399_CLK_I2S0_FRAC, 0, RK3399_XIN12M },
		SET_PARENT
	},
	{
		RK3399_CLK_I2S1_8CH, RK3399_CRU_CLKSEL_CON(29),
		SEL(9, 8), 0,
		{ RK3399_CLK_I2S1_DIV, RK3399_CLK_I2S1_FRAC, 0, RK3399_XIN12M },
		SET_PARENT
	},
	{
		RK3399_CLK_I2S2_8CH, RK3399_CRU_CLKSEL_CON(30),
		SEL(9, 8), 0,
		{ RK3399_CLK_I2S2_DIV, RK3399_CLK_I2S2_FRAC, 0, RK3399_XIN12M },
		SET_PARENT
	},
	{
		RK3399_CLK_I2S_8CH_OUT, RK3399_CRU_CLKSEL_CON(31),
		SEL(2, 2), 0,
		{ RK3399_CLK_I2SOUT_SRC, RK3399_XIN12M },
		SET_PARENT
	},
	{
		RK3399_CLK_MAC, RK3399_CRU_CLKSEL_CON(20),
		SEL(15, 14), DIV(12, 8),
		{ RK3399_PLL_CPLL, RK3399_PLL_GPLL, RK3399_PLL_NPLL }
	},
	{
		RK3399_CLK_UPHY0_TCPDCORE, RK3399_CRU_CLKSEL_CON(64),
		SEL(7, 6), DIV(4, 0),
		{ RK3399_XIN24M, RK3399_CLK_32K, RK3399_PLL_CPLL,
		  RK3399_PLL_GPLL }
	},
	{
		RK3399_CLK_UPHY1_TCPDCORE, RK3399_CRU_CLKSEL_CON(65),
		SEL(7, 6), DIV(4, 0),
		{ RK3399_XIN24M, RK3399_CLK_32K, RK3399_PLL_CPLL,
		  RK3399_PLL_GPLL }
	},
	{
		RK3399_CLK_PCIEPHY_REF, RK3399_CRU_CLKSEL_CON(18),
		SEL(10, 10), 0,
		{ RK3399_XIN24M, RK3399_CLK_PCIEPHY_REF100M },
		SET_PARENT
	},
	{
		RK3399_CLK_PCIEPHY_REF100M, RK3399_CRU_CLKSEL_CON(18),
		0, DIV(15, 11),
		{ RK3399_PLL_NPLL }
	},
	{
		RK3399_DCLK_VOP0, RK3399_CRU_CLKSEL_CON(49),
		SEL(11, 11), 0,
		{ RK3399_DCLK_VOP0_DIV, RK3399_DCLK_VOP0_FRAC },
		SET_PARENT
	},
	{
		RK3399_DCLK_VOP1, RK3399_CRU_CLKSEL_CON(50),
		SEL(11, 11), 0,
		{ RK3399_DCLK_VOP1_DIV, RK3399_DCLK_VOP1_FRAC },
		SET_PARENT
	},
	{
		RK3399_DCLK_VOP0_DIV, RK3399_CRU_CLKSEL_CON(49),
		SEL(9, 8), DIV(7, 0),
		{ RK3399_PLL_VPLL, RK3399_PLL_CPLL, RK3399_PLL_GPLL }
	},
	{
		RK3399_DCLK_VOP1_DIV, RK3399_CRU_CLKSEL_CON(50),
		SEL(9, 8), DIV(7, 0),
		{ RK3399_PLL_VPLL, RK3399_PLL_CPLL, RK3399_PLL_GPLL }
	},
	{
		RK3399_ACLK_PERIPH, RK3399_CRU_CLKSEL_CON(14),
		SEL(7, 7), DIV(4, 0),
		{ RK3399_PLL_CPLL, RK3399_PLL_GPLL }
	},
	{
		RK3399_ACLK_PERILP0, RK3399_CRU_CLKSEL_CON(23),
		SEL(7, 7), DIV(4, 0),
		{ RK3399_PLL_CPLL, RK3399_PLL_GPLL }
	},
	{
		RK3399_ACLK_VIO, RK3399_CRU_CLKSEL_CON(42),
		SEL(7, 6), DIV(4, 0),
		{ RK3399_PLL_CPLL, RK3399_PLL_GPLL, /* RK3399_PLL_PPLL */ }
	},
	{
		RK3399_ACLK_CCI, RK3399_CRU_CLKSEL_CON(5),
		SEL(7, 6), DIV(4, 0),
		{ RK3399_PLL_CPLL, RK3399_PLL_GPLL, RK3399_PLL_NPLL,
		  RK3399_PLL_VPLL }
	},
	{
		RK3399_ACLK_VOP0, RK3399_CRU_CLKSEL_CON(47),
		SEL(7, 6), DIV(4, 0),
		{ RK3399_PLL_VPLL, RK3399_PLL_CPLL, RK3399_PLL_GPLL,
		  RK3399_PLL_NPLL }
	},
	{
		RK3399_ACLK_VOP1, RK3399_CRU_CLKSEL_CON(48),
		SEL(7, 6), DIV(4, 0),
		{ RK3399_PLL_VPLL, RK3399_PLL_CPLL, RK3399_PLL_GPLL,
		  RK3399_PLL_NPLL }
	},
	{
		RK3399_ACLK_HDCP, RK3399_CRU_CLKSEL_CON(42),
		SEL(15, 14), DIV(12, 8),
		{ RK3399_PLL_CPLL, RK3399_PLL_GPLL, /* RK3399_PLL_PPLL */ }
	},
	{
		RK3399_ACLK_GIC_PRE, RK3399_CRU_CLKSEL_CON(56),
		SEL(15, 15), DIV(12, 8),
		{ RK3399_PLL_CPLL, RK3399_PLL_GPLL }
	},
	{
		RK3399_PCLK_PERIPH, RK3399_CRU_CLKSEL_CON(14),
		0, DIV(14, 12),
		{ RK3399_ACLK_PERIPH }
	},
	{
		RK3399_PCLK_PERILP0, RK3399_CRU_CLKSEL_CON(23),
		0, DIV(14, 12),
		{ RK3399_ACLK_PERILP0 }
	},
	{
		RK3399_PCLK_PERILP1, RK3399_CRU_CLKSEL_CON(25),
		0, DIV(10, 8),
		{ RK3399_HCLK_PERILP1 }
	},
	{
		RK3399_PCLK_DDR, RK3399_CRU_CLKSEL_CON(6),
		SEL(15, 15), DIV(12, 8),
		{ RK3399_PLL_CPLL, RK3399_PLL_GPLL }
	},
	{
		RK3399_PCLK_WDT, RK3399_CRU_CLKSEL_CON(57),
		0, DIV(4, 0),
		{ RK3399_PLL_GPLL }
	},
	{
		RK3399_HCLK_PERIPH, RK3399_CRU_CLKSEL_CON(14),
		0, DIV(9, 8),
		{ RK3399_ACLK_PERIPH }
	},
	{
		RK3399_HCLK_PERILP0, RK3399_CRU_CLKSEL_CON(23),
		0, DIV(9, 8),
		{ RK3399_ACLK_PERILP0 }
	},
	{
		RK3399_HCLK_PERILP1, RK3399_CRU_CLKSEL_CON(25),
		SEL(7, 7), DIV(4, 0),
		{ RK3399_PLL_CPLL, RK3399_PLL_GPLL }
	},
	{
		RK3399_HCLK_SDMMC, RK3399_CRU_CLKSEL_CON(13),
		SEL(15, 15), DIV(12, 8),
		{ RK3399_PLL_CPLL, RK3399_PLL_GPLL }
	},
	{
		RK3399_HCLK_VOP0, RK3399_CRU_CLKSEL_CON(47),
		0, DIV(12, 8),
		{ RK3399_ACLK_VOP0 }
	},
	{
		RK3399_HCLK_VOP1, RK3399_CRU_CLKSEL_CON(48),
		0, DIV(12, 8),
		{ RK3399_ACLK_VOP1 }
	},
	{
		RK3399_CLK_I2S0_DIV, RK3399_CRU_CLKSEL_CON(28),
		SEL(7, 7), DIV(6, 0),
		{ RK3399_PLL_CPLL, RK3399_PLL_GPLL }
	},
	{
		RK3399_CLK_I2S1_DIV, RK3399_CRU_CLKSEL_CON(29),
		SEL(7, 7), DIV(6, 0),
		{ RK3399_PLL_CPLL, RK3399_PLL_GPLL }
	},
	{
		RK3399_CLK_I2S2_DIV, RK3399_CRU_CLKSEL_CON(30),
		SEL(7, 7), DIV(6, 0),
		{ RK3399_PLL_CPLL, RK3399_PLL_GPLL }
	},
	{
		RK3399_CLK_I2SOUT_SRC, RK3399_CRU_CLKSEL_CON(31),
		SEL(1, 0), 0,
		{ RK3399_CLK_I2S0_8CH, RK3399_CLK_I2S1_8CH,
		  RK3399_CLK_I2S2_8CH },
		SET_PARENT
	},
	{
		/* Sentinel */
	}
};

/* Some of our parent clocks live in the PMUCRU. */
struct rkclock_softc *rk3399_pmucru_sc;

void
rk3399_init(struct rkclock_softc *sc)
{
	int i;

	/* PMUCRU instance should attach before us. */
	KASSERT(rk3399_pmucru_sc != NULL);

	/*
	 * The U-Boot shipped on the Theobroma Systems RK3399-Q7
	 * module is buggy and sets the parent of the clock for the
	 * "big" cluster to LPLL.  Undo that mistake here such that
	 * the clocks of both clusters are independent.
	 */
	HWRITE4(sc, RK3399_CRU_CLKSEL_CON(2),
	    RK3399_CRU_CORE_PLL_SEL_MASK << 16 |
	    RK3399_CRU_CORE_PLL_SEL_BPLL);

	/* The code below assumes all clocks are enabled.  Check this!. */
	for (i = 0; i <= 34; i++) {
		if (HREAD4(sc, RK3399_CRU_CLKGATE_CON(i)) != 0x00000000) {
			printf("CRU_CLKGATE_CON%d: 0x%08x\n", i,
			    HREAD4(sc, RK3399_CRU_CLKGATE_CON(i)));
		}
	}

	sc->sc_clocks = rk3399_clocks;
}

uint32_t
rk3399_get_pll(struct rkclock_softc *sc, bus_size_t base)
{
	uint32_t fbdiv, postdiv1, postdiv2, refdiv;
	uint32_t pll_work_mode;
	uint32_t reg;

	reg = HREAD4(sc, base + 0x000c);
	pll_work_mode = reg & RK3399_CRU_PLL_PLL_WORK_MODE_MASK;
	if (pll_work_mode == RK3399_CRU_PLL_PLL_WORK_MODE_SLOW)
		return 24000000;
	if (pll_work_mode == RK3399_CRU_PLL_PLL_WORK_MODE_DEEP_SLOW)
		return 32768;

	reg = HREAD4(sc, base + 0x0000);
	fbdiv = (reg & RK3399_CRU_PLL_FBDIV_MASK) >>
	    RK3399_CRU_PLL_FBDIV_SHIFT;
	reg = HREAD4(sc, base + 0x0004);
	postdiv2 = (reg & RK3399_CRU_PLL_POSTDIV2_MASK) >>
	    RK3399_CRU_PLL_POSTDIV2_SHIFT;
	postdiv1 = (reg & RK3399_CRU_PLL_POSTDIV1_MASK) >>
	    RK3399_CRU_PLL_POSTDIV1_SHIFT;
	refdiv = (reg & RK3399_CRU_PLL_REFDIV_MASK) >>
	    RK3399_CRU_PLL_REFDIV_SHIFT;
	return 24000000ULL * fbdiv / refdiv / postdiv1 / postdiv2;
}

int
rk3399_set_pll(struct rkclock_softc *sc, bus_size_t base, uint32_t freq)
{
	uint32_t fbdiv, postdiv1, postdiv2, refdiv;

	/*
	 * It is not clear whether all combinations of the clock
	 * dividers result in a stable clock.  Therefore this function
	 * only supports a limited set of PLL clock rates.  For now
	 * this set covers all the CPU frequencies supported by the
	 * Linux kernel.
	 */
	switch (freq) {
	case 2208000000U:
	case 2184000000U:
	case 2088000000U:
	case 2040000000U:
	case 2016000000U:
	case 1992000000U:
	case 1896000000U:
	case 1800000000U:
	case 1704000000U:
	case 1608000000U:
	case 1512000000U:
	case 1488000000U:
	case 1416000000U:
	case 1200000000U:
		postdiv1 = postdiv2 = refdiv = 1;
		break;
	case 1008000000U:
	case 816000000U:
	case 696000000U:
		postdiv1 = 2; postdiv2 = refdiv = 1;
		break;
	case 676000000U:
		postdiv1 = 2; postdiv2 = 1; refdiv = 3;
		break;
	case 1000000000U:
	case 800000000U:
	case 600000000U:
		postdiv1 = 3; postdiv2 = refdiv = 1;
		break;
	case 594000000U:
		postdiv1 = 4; postdiv2 = refdiv = 1;
		break;
	case 408000000U:
		postdiv1 = postdiv2 = 2; refdiv = 1;
		break;
	case 297000000U:
	case 216000000U:
		postdiv1 = 4; postdiv2 = 2; refdiv = 1;
		break;
	case 148500000U:
	case 96000000U:
		postdiv1 = postdiv2 = 4; refdiv = 1;
		break;
	case 74250000U:
		postdiv1 = postdiv2 = 4; refdiv = 2;
		break;
	case 65000000U:
	case 54000000U:
	case 27000000U:
		postdiv1 = 6; postdiv2 = 4; refdiv = 1;
		break;
	default:
		printf("%s: %d Hz\n", __func__, freq);
		return -1;
	}

	/* Calculate feedback divider. */
	fbdiv = freq * postdiv1 * postdiv2 * refdiv / 24000000;

	/*
	 * Select slow mode to guarantee a stable clock while we're
	 * adjusting the PLL.
	 */
	HWRITE4(sc, base + 0x000c,
	    RK3399_CRU_PLL_PLL_WORK_MODE_MASK << 16 |
	    RK3399_CRU_PLL_PLL_WORK_MODE_SLOW);

	/* Set PLL rate. */
	HWRITE4(sc, base + 0x0000,
	    RK3399_CRU_PLL_FBDIV_MASK << 16 |
	    fbdiv << RK3399_CRU_PLL_FBDIV_SHIFT);
	HWRITE4(sc, base + 0x0004,
	    RK3399_CRU_PLL_POSTDIV2_MASK << 16 |
	    postdiv2 << RK3399_CRU_PLL_POSTDIV2_SHIFT |
	    RK3399_CRU_PLL_POSTDIV1_MASK << 16 |
	    postdiv1 << RK3399_CRU_PLL_POSTDIV1_SHIFT |
	    RK3399_CRU_PLL_REFDIV_MASK << 16 |
	    refdiv << RK3399_CRU_PLL_REFDIV_SHIFT);

	/* Wait for PLL to stabilize. */
	while ((HREAD4(sc, base + 0x0008) & RK3399_CRU_PLL_PLL_LOCK) == 0)
		delay(10);

	/* Switch back to normal mode. */
	HWRITE4(sc, base + 0x000c,
	    RK3399_CRU_PLL_PLL_WORK_MODE_MASK << 16 |
	    RK3399_CRU_PLL_PLL_WORK_MODE_NORMAL);

	return 0;
}

uint32_t
rk3399_armclk_parent(uint32_t mux)
{
	switch (mux) {
	case 0:
		return RK3399_PLL_ALPLL;
	case 1:
		return RK3399_PLL_ABPLL;
	case 2:
		return RK3399_PLL_DPLL;
	case 3:
		return RK3399_PLL_GPLL;
	}

	return 0;
}

uint32_t
rk3399_get_armclk(struct rkclock_softc *sc, bus_size_t clksel)
{
	uint32_t reg, mux, div_con;
	uint32_t idx;

	reg = HREAD4(sc, clksel);
	mux = (reg & RK3399_CRU_CORE_PLL_SEL_MASK) >>
	    RK3399_CRU_CORE_PLL_SEL_SHIFT;
	div_con = (reg & RK3399_CRU_CLK_CORE_DIV_CON_MASK) >>
	    RK3399_CRU_CLK_CORE_DIV_CON_SHIFT;
	idx = rk3399_armclk_parent(mux);
	
	return rk3399_get_frequency(sc, &idx) / (div_con + 1);
}

int
rk3399_set_armclk(struct rkclock_softc *sc, bus_size_t clksel, uint32_t freq)
{
	uint32_t reg, mux;
	uint32_t old_freq, div;
	uint32_t idx;

	old_freq = rk3399_get_armclk(sc, clksel);
	if (freq == old_freq)
		return 0;

	reg = HREAD4(sc, clksel);
	mux = (reg & RK3399_CRU_CORE_PLL_SEL_MASK) >>
	    RK3399_CRU_CORE_PLL_SEL_SHIFT;
	idx = rk3399_armclk_parent(mux);

	/* Keep the atclk_core and pclk_dbg clocks at or below 200 MHz. */
	div = 1;
	while (freq / (div + 1) > 200000000)
		div++;

	/* When ramping up, set clock dividers first. */
	if (freq > old_freq) {
		HWRITE4(sc, clksel,
		    RK3399_CRU_CLK_CORE_DIV_CON_MASK << 16 |
		    0 << RK3399_CRU_CLK_CORE_DIV_CON_SHIFT |
		    RK3399_CRU_ACLKM_CORE_DIV_CON_MASK << 16 |
		    1 << RK3399_CRU_ACLKM_CORE_DIV_CON_SHIFT);
		HWRITE4(sc, clksel + 0x0004,
		    RK3399_CRU_PCLK_DBG_DIV_CON_MASK << 16 |
		    div << RK3399_CRU_PCLK_DBG_DIV_CON_SHIFT |
		    RK3399_CRU_ATCLK_CORE_DIV_CON_MASK << 16 |
		    div << RK3399_CRU_ATCLK_CORE_DIV_CON_SHIFT);
	}

	rk3399_set_frequency(sc, &idx, freq);

	/* When ramping down, set clock dividers last. */
	if (freq < old_freq) {
		HWRITE4(sc, clksel,
		    RK3399_CRU_CLK_CORE_DIV_CON_MASK << 16 |
		    0 << RK3399_CRU_CLK_CORE_DIV_CON_SHIFT |
		    RK3399_CRU_ACLKM_CORE_DIV_CON_MASK << 16 |
		    1 << RK3399_CRU_ACLKM_CORE_DIV_CON_SHIFT);
		HWRITE4(sc, clksel + 0x0004,
		    RK3399_CRU_PCLK_DBG_DIV_CON_MASK << 16 |
		    div << RK3399_CRU_PCLK_DBG_DIV_CON_SHIFT |
		    RK3399_CRU_ATCLK_CORE_DIV_CON_MASK << 16 |
		    div << RK3399_CRU_ATCLK_CORE_DIV_CON_SHIFT);
	}

	return 0;
}

uint32_t
rk3399_get_frac(struct rkclock_softc *sc, uint32_t parent, bus_size_t base)
{
	uint32_t parent_freq, frac;
	uint16_t n, d;

	frac = HREAD4(sc, base);
	n = frac >> 16;
	d = frac & 0xffff;
	if (n == 0 || d == 0)
		n = d = 1;
	parent_freq = sc->sc_cd.cd_get_frequency(sc, &parent);
	return ((uint64_t)parent_freq * n) / d;
}

int
rk3399_set_frac(struct rkclock_softc *sc, uint32_t parent, bus_size_t base,
    uint32_t freq)
{
	uint32_t n, d;
	uint32_t p0, p1, p2;
	uint32_t q0, q1, q2;
	uint32_t a, tmp;

	n = freq;
	d = sc->sc_cd.cd_get_frequency(sc, &parent);

	/*
	 * The denominator needs to be at least 20 times the numerator
	 * for a stable clock.
	 */
	if (n == 0 || d == 0 || d < 20 * n)
		return -1;

	/*
	 * This is a simplified implementation of the algorithm to
	 * calculate the best rational approximation using continued
	 * fractions.
	 */

	p0 = q1 = 0;
	p1 = q0 = 1;

	while (d != 0) {
		/*
		 * Calculate next coefficient in the continued
		 * fraction and keep track of the remainder.
		 */
		tmp = d;
		a = n / d;
		d = n % d;
		n = tmp;

		/*
		 * Calculate next approximation in the series based on
		 * the current coefficient.
		 */
		p2 = p0 + a * p1;
		q2 = q0 + a * q1;

		/*
		 * Terminate if we reached the maximum allowed
		 * denominator.
		 */
		if (q2 > 0xffff)
			break;

		p0 = p1; p1 = p2; 
		q0 = q1; q1 = q2;
	}

	HWRITE4(sc, base, p1 << 16 | q1);
	return 0;
}

uint32_t
rk3399_get_frequency(void *cookie, uint32_t *cells)
{
	struct rkclock_softc *sc = cookie;
	uint32_t idx = cells[0];

	switch (idx) {
	case RK3399_PLL_ALPLL:
		return rk3399_get_pll(sc, RK3399_CRU_LPLL_CON(0));
	case RK3399_PLL_ABPLL:
		return rk3399_get_pll(sc, RK3399_CRU_BPLL_CON(0));
	case RK3399_PLL_DPLL:
		return rk3399_get_pll(sc, RK3399_CRU_DPLL_CON(0));
	case RK3399_PLL_CPLL:
		return rk3399_get_pll(sc, RK3399_CRU_CPLL_CON(0));
	case RK3399_PLL_GPLL:
		return rk3399_get_pll(sc, RK3399_CRU_GPLL_CON(0));
	case RK3399_PLL_NPLL:
		return rk3399_get_pll(sc, RK3399_CRU_NPLL_CON(0));
	case RK3399_PLL_VPLL:
		return rk3399_get_pll(sc, RK3399_CRU_VPLL_CON(0));
	case RK3399_ARMCLKL:
		return rk3399_get_armclk(sc, RK3399_CRU_CLKSEL_CON(0));
	case RK3399_ARMCLKB:
		return rk3399_get_armclk(sc, RK3399_CRU_CLKSEL_CON(2));
	case RK3399_XIN24M:
		return 24000000;
	case RK3399_CLK_32K:
		return 32768;
	case RK3399_XIN12M:
		return 12000000;
	case RK3399_CLK_I2S0_FRAC:
		return rk3399_get_frac(sc, RK3399_CLK_I2S0_DIV,
		    RK3399_CRU_CLKSEL_CON(96));
	case RK3399_CLK_I2S1_FRAC:
		return rk3399_get_frac(sc, RK3399_CLK_I2S1_DIV,
		    RK3399_CRU_CLKSEL_CON(97));
	case RK3399_CLK_I2S2_FRAC:
		return rk3399_get_frac(sc, RK3399_CLK_I2S2_DIV,
		    RK3399_CRU_CLKSEL_CON(98));
	default:
		break;
	}

	return rkclock_get_frequency(sc, idx);
}

int
rk3399_set_frequency(void *cookie, uint32_t *cells, uint32_t freq)
{
	struct rkclock_softc *sc = cookie;
	uint32_t idx = cells[0];

	switch (idx) {
	case RK3399_PLL_ALPLL:
		return rk3399_set_pll(sc, RK3399_CRU_LPLL_CON(0), freq);
	case RK3399_PLL_ABPLL:
		return rk3399_set_pll(sc, RK3399_CRU_BPLL_CON(0), freq);
	case RK3399_PLL_CPLL:
		return rk3399_set_pll(sc, RK3399_CRU_CPLL_CON(0), freq);
	case RK3399_PLL_GPLL:
		return rk3399_set_pll(sc, RK3399_CRU_GPLL_CON(0), freq);
	case RK3399_PLL_NPLL:
		return rk3399_set_pll(sc, RK3399_CRU_NPLL_CON(0), freq);
	case RK3399_PLL_VPLL:
		return rk3399_set_pll(sc, RK3399_CRU_VPLL_CON(0), freq);
	case RK3399_ARMCLKL:
		return rk3399_set_armclk(sc, RK3399_CRU_CLKSEL_CON(0), freq);
	case RK3399_ARMCLKB:
		return rk3399_set_armclk(sc, RK3399_CRU_CLKSEL_CON(2), freq);
	case RK3399_CLK_I2S0_8CH:
		rkclock_set_parent(sc, idx, RK3399_CLK_I2S0_FRAC);
		return rkclock_set_frequency(sc, idx, freq);
	case RK3399_CLK_I2S1_8CH:
		rkclock_set_parent(sc, idx, RK3399_CLK_I2S1_FRAC);
		return rkclock_set_frequency(sc, idx, freq);
	case RK3399_CLK_I2S2_8CH:
		rkclock_set_parent(sc, idx, RK3399_CLK_I2S2_FRAC);
		return rkclock_set_frequency(sc, idx, freq);
	case RK3399_XIN12M:
		if (freq / (1000 * 1000) != 12)
			return -1;
		return 0;
	case RK3399_CLK_I2S0_FRAC:
		return rk3399_set_frac(sc, RK3399_CLK_I2S0_DIV,
		    RK3399_CRU_CLKSEL_CON(96), freq);
	case RK3399_CLK_I2S1_FRAC:
		return rk3399_set_frac(sc, RK3399_CLK_I2S1_DIV,
		    RK3399_CRU_CLKSEL_CON(97), freq);
	case RK3399_CLK_I2S2_FRAC:
		return rk3399_set_frac(sc, RK3399_CLK_I2S2_DIV,
		    RK3399_CRU_CLKSEL_CON(98), freq);
	default:
		break;
	}

	return rkclock_set_frequency(sc, idx, freq);
}


int
rk3399_set_parent(void *cookie, uint32_t *cells, uint32_t *pcells)
{
	struct rkclock_softc *sc = cookie;

	if (pcells[0] != sc->sc_phandle)
		return -1;

	return rkclock_set_parent(sc, cells[0], pcells[1]);
}

void
rk3399_enable(void *cookie, uint32_t *cells, int on)
{
	struct rkclock_softc *sc = cookie;
	uint32_t idx = cells[0];

	/*
	 * All clocks are enabled upon hardware reset, but on some boards the
	 * firmware will disable some of them.  Handle those here.
	 */
	if (!on) {
		printf("%s: 0x%08x\n", __func__, idx);
		return;
	}

	switch (idx) {
	case RK3399_CLK_USB2PHY0_REF:
		HWRITE4(sc, RK3399_CRU_CLKGATE_CON(6), (1 << 5) << 16);
		break;
	case RK3399_CLK_USB2PHY1_REF:
		HWRITE4(sc, RK3399_CRU_CLKGATE_CON(6), (1 << 6) << 16);
		break;
	case RK3399_CLK_UPHY0_TCPDPHY_REF:
		HWRITE4(sc, RK3399_CRU_CLKGATE_CON(13), (1 << 4) << 16);
		break;
	case RK3399_CLK_UPHY0_TCPDCORE:
		HWRITE4(sc, RK3399_CRU_CLKGATE_CON(13), (1 << 5) << 16);
		break;
	case RK3399_CLK_UPHY1_TCPDPHY_REF:
		HWRITE4(sc, RK3399_CRU_CLKGATE_CON(13), (1 << 6) << 16);
		break;
	case RK3399_CLK_UPHY1_TCPDCORE:
		HWRITE4(sc, RK3399_CRU_CLKGATE_CON(13), (1 << 7) << 16);
		break;
	case RK3399_ACLK_GMAC:
		HWRITE4(sc, RK3399_CRU_CLKGATE_CON(32), (1 << 0) << 16);
		break;
	case RK3399_PCLK_GMAC:
		HWRITE4(sc, RK3399_CRU_CLKGATE_CON(32), (1 << 2) << 16);
		break;
	case RK3399_CLK_MAC:
		HWRITE4(sc, RK3399_CRU_CLKGATE_CON(5), (1 << 5) << 16);
		break;
	case RK3399_CLK_MAC_RX:
		HWRITE4(sc, RK3399_CRU_CLKGATE_CON(5), (1 << 8) << 16);
		break;
	case RK3399_CLK_MAC_TX:
		HWRITE4(sc, RK3399_CRU_CLKGATE_CON(5), (1 << 9) << 16);
		break;
	}
}

void
rk3399_reset(void *cookie, uint32_t *cells, int on)
{
	struct rkclock_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t mask = (1 << (idx % 16));

	HWRITE4(sc, RK3399_CRU_SOFTRST_CON(idx / 16),
	    mask << 16 | (on ? mask : 0));
}

/* PMUCRU */

const struct rkclock rk3399_pmu_clocks[] = {
	{
		RK3399_CLK_I2C0, RK3399_PMUCRU_CLKSEL_CON(2),
		0, DIV(6, 0),
		{ RK3399_PLL_PPLL }
	},
	{
		RK3399_CLK_I2C4, RK3399_PMUCRU_CLKSEL_CON(3),
		0, DIV(6, 0),
		{ RK3399_PLL_PPLL }
	},
	{
		RK3399_CLK_I2C8, RK3399_PMUCRU_CLKSEL_CON(2),
		0, DIV(14, 8),
		{ RK3399_PLL_PPLL }
	},
	{
		RK3399_PCLK_RKPWM, RK3399_PMUCRU_CLKSEL_CON(0),
		0, DIV(6, 0),
		{ RK3399_PLL_PPLL }
	},
	{
		/* Sentinel */
	}
};
	
void
rk3399_pmu_init(struct rkclock_softc *sc)
{
	sc->sc_clocks = rk3399_pmu_clocks;
	rk3399_pmucru_sc = sc;
}

uint32_t
rk3399_pmu_get_frequency(void *cookie, uint32_t *cells)
{
	struct rkclock_softc *sc = cookie;
	uint32_t idx = cells[0];

	switch (idx) {
	case RK3399_PLL_PPLL:
		return rk3399_get_pll(sc, RK3399_PMUCRU_PPLL_CON(0));
	default:
		break;
	}

	return rkclock_get_frequency(sc, idx);
}

int
rk3399_pmu_set_frequency(void *cookie, uint32_t *cells, uint32_t freq)
{
	struct rkclock_softc *sc = cookie;
	uint32_t idx = cells[0];

	switch (idx) {
	case RK3399_PLL_PPLL:
		return rk3399_set_pll(sc, RK3399_PMUCRU_PPLL_CON(0), freq);
	default:
		break;
	}

	return rkclock_set_frequency(sc, idx, freq);
}

void
rk3399_pmu_enable(void *cookie, uint32_t *cells, int on)
{
	uint32_t idx = cells[0];

	switch (idx) {
	case RK3399_CLK_I2C0:
	case RK3399_CLK_I2C4:
	case RK3399_CLK_I2C8:
	case RK3399_PCLK_I2C0:
	case RK3399_PCLK_I2C4:
	case RK3399_PCLK_I2C8:
	case RK3399_PCLK_RKPWM:
		/* Enabled by default. */
		break;
	default:
		printf("%s: 0x%08x\n", __func__, idx);
		break;
	}
}

void
rk3399_pmu_reset(void *cookie, uint32_t *cells, int on)
{
	uint32_t idx = cells[0];

	printf("%s: 0x%08x\n", __func__, idx);
}

/* 
 * Rockchip RK3528 
 */

const struct rkclock rk3528_clocks[] = {
	{
		RK3528_CLK_MATRIX_50M_SRC, RK3528_CRU_CLKSEL_CON(0),
		0, DIV(6, 2),
		{ RK3528_PLL_CPLL }
	},
	{
		RK3528_CLK_MATRIX_100M_SRC, RK3528_CRU_CLKSEL_CON(0),
		0, DIV(11, 7),
		{ RK3528_PLL_CPLL }
	},
	{
		RK3528_CLK_MATRIX_200M_SRC, RK3528_CRU_CLKSEL_CON(1),
		0, DIV(9, 5),
		{ RK3528_PLL_GPLL }
	},
	{
		RK3528_CLK_PWM0, RK3528_CRU_CLKSEL_CON(44),
		SEL(7, 6), 0,
		{ RK3528_CLK_MATRIX_100M_SRC, RK3528_CLK_MATRIX_50M_SRC,
		  RK3528_XIN24M }
	},
	{
		RK3528_CLK_PWM1, RK3528_CRU_CLKSEL_CON(44),
		SEL(9, 8), 0,
		{ RK3528_CLK_MATRIX_100M_SRC, RK3528_CLK_MATRIX_50M_SRC,
		  RK3528_XIN24M }
	},
	{
		RK3528_CLK_PPLL_125M_MATRIX, RK3528_CRU_CLKSEL_CON(60),
		0, DIV(14, 10),
		{ RK3528_PLL_PPLL }
	},
	{
		RK3528_CCLK_SRC_EMMC, RK3528_CRU_CLKSEL_CON(62),
		SEL(7, 6), DIV(5, 0),
		{ RK3528_PLL_GPLL, RK3528_PLL_CPLL, RK3528_XIN24M }
	},
	{
		RK3528_BCLK_EMMC, RK3528_CRU_CLKSEL_CON(62),
		SEL(9, 8), 0,
		{ RK3528_CLK_MATRIX_200M_SRC, RK3528_CLK_MATRIX_100M_SRC,
		  RK3528_CLK_MATRIX_50M_SRC, RK3528_XIN24M }
	},
	{
		RK3528_TCLK_EMMC, 0, 0, 0,
		{ RK3528_XIN24M }
	},
	{
		RK3528_CLK_GMAC1_SRC_VPU, 0, 0, 0,
		{ RK3528_CLK_PPLL_125M_MATRIX }
	},
	{
		RK3528_CLK_I2C1, RK3528_CRU_CLKSEL_CON(79),
		SEL(10, 9), 0,
		{ RK3528_CLK_MATRIX_200M_SRC, RK3528_CLK_MATRIX_100M_SRC,
		  RK3528_CLK_MATRIX_50M_SRC, RK3528_XIN24M }
	},
	{
		RK3528_CCLK_SRC_SDMMC0, RK3528_CRU_CLKSEL_CON(85),
		SEL(7, 6), DIV(5, 0),
		{ RK3528_PLL_GPLL, RK3528_PLL_CPLL, RK3528_XIN24M }
	},
	{
		/* Sentinel */
	}
};

void
rk3528_init(struct rkclock_softc *sc)
{
	int i;

	/* The code below assumes all clocks are enabled.  Check this!. */
	for (i = 0; i <= 46; i++) {
		if (HREAD4(sc, RK3528_CRU_GATE_CON(i)) != 0x00000000) {
			printf("CRU_GATE_CON%d: 0x%08x\n", i,
			    HREAD4(sc, RK3528_CRU_GATE_CON(i)));
		}
	}

	sc->sc_clocks = rk3528_clocks;
}

uint32_t
rk3528_get_frequency(void *cookie, uint32_t *cells)
{
	struct rkclock_softc *sc = cookie;
	uint32_t idx = cells[0];

	switch (idx) {
	case RK3528_PLL_CPLL:
		return rk3328_get_pll(sc, RK3528_CRU_PLL_CON(8));
	case RK3528_PLL_GPLL:
		return rk3328_get_pll(sc, RK3528_CRU_PLL_CON(24));
	case RK3528_PLL_PPLL:
		return rk3328_get_pll(sc, RK3528_PCIE_CRU_PLL_CON(32));
	case RK3528_XIN24M:
		return 24000000;
	default:
		break;
	}

	return rkclock_get_frequency(sc, idx);
}

int
rk3528_set_frequency(void *cookie, uint32_t *cells, uint32_t freq)
{
	struct rkclock_softc *sc = cookie;
	uint32_t idx = cells[0];

	return rkclock_set_frequency(sc, idx, freq);
}

int
rk3528_set_parent(void *cookie, uint32_t *cells, uint32_t *pcells)
{
	struct rkclock_softc *sc = cookie;

	return rkclock_set_parent(sc, cells[0], pcells[1]);
}

void
rk3528_enable(void *cookie, uint32_t *cells, int on)
{
	uint32_t idx = cells[0];

	/* All clocks are enabled upon hardware reset. */
	if (!on) {
		printf("%s: 0x%08x\n", __func__, idx);
		return;
	}
}

void
rk3528_reset(void *cookie, uint32_t *cells, int on)
{
	struct rkclock_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t bit, mask, reg;

	switch (idx) {
	case RK3528_SRST_C_EMMC:
		reg = RK3528_CRU_SOFTRST_CON(25);
		bit = 15;
		break;
	case RK3528_SRST_H_EMMC:
		reg = RK3528_CRU_SOFTRST_CON(26);
		bit = 0;
		break;
	case RK3528_SRST_A_EMMC:
		reg = RK3528_CRU_SOFTRST_CON(26);
		bit = 1;
		break;
	case RK3528_SRST_B_EMMC:
		reg = RK3528_CRU_SOFTRST_CON(26);
		bit = 2;
		break;
	case RK3528_SRST_T_EMMC:
		reg = RK3528_CRU_SOFTRST_CON(26);
		bit = 3;
		break;
	case RK3528_SRST_A_MAC:
		reg = RK3528_CRU_SOFTRST_CON(28);
		bit = 5;
		break;
	case RK3528_SRST_H_SDMMC0:
		reg = RK3528_CRU_SOFTRST_CON(42);
		bit = 9;
		break;
	default:
		printf("%s: 0x%08x\n", __func__, idx);
		return;
	}

	mask = (1 << bit);
	HWRITE4(sc, reg, mask << 16 | (on ? mask : 0));
}

/* 
 * Rockchip RK3568 
 */

const struct rkclock rk3568_clocks[] = {
	{
		RK3568_BCLK_EMMC, RK3568_CRU_CLKSEL_CON(28),
		SEL(9, 8), 0,
		{ RK3568_GPLL_200M, RK3568_GPLL_150M, RK3568_CPLL_125M }
	},
	{
		RK3568_CCLK_EMMC, RK3568_CRU_CLKSEL_CON(28),
		SEL(14, 12), 0,
		{ RK3568_XIN24M, RK3568_GPLL_200M, RK3568_GPLL_150M,
		  RK3568_CPLL_100M, RK3568_CPLL_50M, RK3568_CLK_OSC0_DIV_375K }
	},
	{
		RK3568_TCLK_EMMC, 0, 0, 0,
		{ RK3568_XIN24M }
	},

	{
		RK3568_ACLK_PHP, RK3568_CRU_CLKSEL_CON(30),
		SEL(1, 0), 0,
		{ RK3568_GPLL_300M, RK3568_GPLL_200M,
		  RK3568_GPLL_100M, RK3568_XIN24M }
	},
	{
		RK3568_PCLK_PHP, RK3568_CRU_CLKSEL_CON(30),
		0, DIV(7, 4),
		{ RK3568_ACLK_PHP }
	},
	{
		RK3568_CLK_SDMMC0, RK3568_CRU_CLKSEL_CON(30),
		SEL(10, 8), 0,
		{ RK3568_XIN24M, RK3568_GPLL_400M, RK3568_GPLL_300M,
		  RK3568_CPLL_100M, RK3568_CPLL_50M, RK3568_CLK_OSC0_DIV_750K }
	},
	{
		RK3568_CLK_SDMMC1, RK3568_CRU_CLKSEL_CON(30),
		SEL(14, 12), 0,
		{ RK3568_XIN24M, RK3568_GPLL_400M, RK3568_GPLL_300M,
		  RK3568_CPLL_100M, RK3568_CPLL_50M, RK3568_CLK_OSC0_DIV_750K }
	},
	{
		RK3568_CLK_SDMMC2, RK3568_CRU_CLKSEL_CON(32),
		SEL(10, 8), 0,
		{ RK3568_XIN24M, RK3568_GPLL_400M, RK3568_GPLL_300M,
		  RK3568_CPLL_100M, RK3568_CPLL_50M, RK3568_CLK_OSC0_DIV_750K }
	},
	{
		RK3568_ACLK_GMAC0, 0, 0, 0,
		{ RK3568_ACLK_PHP }
	},
	{
		RK3568_PCLK_GMAC0, 0, 0, 0,
		{ RK3568_PCLK_PHP }
	},
	{
		RK3568_CLK_MAC0_2TOP, RK3568_CRU_CLKSEL_CON(31),
		SEL(9, 8), 0,
		{ RK3568_CPLL_125M, RK3568_CPLL_50M,
		  RK3568_CPLL_25M, RK3568_XIN24M }
	},
	{
		RK3568_CLK_MAC0_REFOUT, 0, 0, 0,
		{ RK3568_CLK_MAC0_2TOP }
	},
	{
		RK3568_CLK_GMAC0_PTP_REF, RK3568_CRU_CLKSEL_CON(31),
		SEL(13, 12), 0,
		{ RK3568_CPLL_62P5M, RK3568_GPLL_100M,
		  RK3568_CPLL_50M, RK3568_XIN24M }
	},
	{
		RK3568_ACLK_USB, RK3568_CRU_CLKSEL_CON(32),
		SEL(1, 0), 0,
		{ RK3568_GPLL_300M, RK3568_GPLL_200M,
		  RK3568_GPLL_100M, RK3568_XIN24M }
	},
	{
		RK3568_PCLK_USB, RK3568_CRU_CLKSEL_CON(32),
		0, DIV(7, 4),
		{ RK3568_ACLK_USB }
	},
	{
		RK3568_ACLK_GMAC1, 0, 0, 0,
		{ RK3568_ACLK_USB }
	},
	{
		RK3568_PCLK_GMAC1, 0, 0, 0,
		{ RK3568_PCLK_USB }
	},
	{
		RK3568_CLK_MAC1_2TOP, RK3568_CRU_CLKSEL_CON(33),
		SEL(9, 8), 0,
		{ RK3568_CPLL_125M, RK3568_CPLL_50M,
		  RK3568_CPLL_25M, RK3568_XIN24M }
	},
	{
		RK3568_CLK_MAC1_REFOUT, 0, 0, 0,
		{ RK3568_CLK_MAC1_2TOP }
	},
	{
		RK3568_CLK_GMAC1_PTP_REF, RK3568_CRU_CLKSEL_CON(33),
		SEL(13, 12), 0,
		{ RK3568_CPLL_62P5M, RK3568_GPLL_100M,
		  RK3568_CPLL_50M, RK3568_XIN24M }
	},
	{
		RK3568_CLK_TSADC_TSEN, RK3568_CRU_CLKSEL_CON(51),
		SEL(5, 4), DIV(2, 0),
		{ RK3568_XIN24M, RK3568_GPLL_100M, RK3568_CPLL_100M }
	},
	{
		RK3568_CLK_TSADC, RK3568_CRU_CLKSEL_CON(51),
		0, DIV(14, 8),
		{ RK3568_CLK_TSADC_TSEN }
	},
	{
		RK3568_SCLK_UART1, RK3568_CRU_CLKSEL_CON(52),
		SEL(13, 12), 0,
		{ 0, 0, RK3568_XIN24M }
	},
	{
		RK3568_SCLK_UART2, RK3568_CRU_CLKSEL_CON(54),
		SEL(13, 12), 0,
		{ 0, 0, RK3568_XIN24M }
	},
	{
		RK3568_SCLK_UART3, RK3568_CRU_CLKSEL_CON(56),
		SEL(13, 12), 0,
		{ 0, 0, RK3568_XIN24M }
	},
	{
		RK3568_SCLK_UART4, RK3568_CRU_CLKSEL_CON(58),
		SEL(13, 12), 0,
		{ 0, 0, RK3568_XIN24M }
	},
	{
		RK3568_SCLK_UART5, RK3568_CRU_CLKSEL_CON(60),
		SEL(13, 12), 0,
		{ 0, 0, RK3568_XIN24M }
	},
	{
		RK3568_SCLK_UART6, RK3568_CRU_CLKSEL_CON(62),
		SEL(13, 12), 0,
		{ 0, 0, RK3568_XIN24M }
	},
	{
		RK3568_SCLK_UART7, RK3568_CRU_CLKSEL_CON(64),
		SEL(13, 12), 0,
		{ 0, 0, RK3568_XIN24M }
	},
	{
		RK3568_SCLK_UART8, RK3568_CRU_CLKSEL_CON(66),
		SEL(13, 12), 0,
		{ 0, 0, RK3568_XIN24M }
	},
	{
		RK3568_SCLK_UART9, RK3568_CRU_CLKSEL_CON(68),
		SEL(13, 12), 0,
		{ 0, 0, RK3568_XIN24M }
	},
	{
		RK3568_CLK_I2C, RK3568_CRU_CLKSEL_CON(71),
		SEL(9, 8), 0,
		{ 0, 0, RK3568_XIN24M }
	},
	{
		RK3568_CLK_I2C1, 0, 0, 0,
		{ RK3568_CLK_I2C }
	},
	{
		RK3568_CLK_I2C2, 0, 0, 0,
		{ RK3568_CLK_I2C }
	},
	{
		RK3568_CLK_I2C3, 0, 0, 0,
		{ RK3568_CLK_I2C }
	},
	{
		RK3568_CLK_I2C4, 0, 0, 0,
		{ RK3568_CLK_I2C }
	},
	{
		RK3568_CLK_I2C5, 0, 0, 0,
		{ RK3568_CLK_I2C }
	},
	{
		RK3568_CLK_SPI0, RK3568_CRU_CLKSEL_CON(72),
		SEL(1, 0), 0,
		{ RK3568_GPLL_200M, RK3568_XIN24M, RK3568_CPLL_100M }
	},
	{
		RK3568_CLK_SPI1, RK3568_CRU_CLKSEL_CON(72),
		SEL(3, 2), 0,
		{ RK3568_GPLL_200M, RK3568_XIN24M, RK3568_CPLL_100M }
	},
	{
		RK3568_CLK_SPI2, RK3568_CRU_CLKSEL_CON(72),
		SEL(5, 4), 0,
		{ RK3568_GPLL_200M, RK3568_XIN24M, RK3568_CPLL_100M }
	},
	{
		RK3568_CLK_SPI3, RK3568_CRU_CLKSEL_CON(72),
		SEL(7, 6), 0,
		{ RK3568_GPLL_200M, RK3568_XIN24M, RK3568_CPLL_100M }
	},
	{
		RK3568_SCLK_GMAC0, RK3568_CRU_CLKSEL_CON(31),
		SEL(2, 2), 0,
		{ RK3568_CLK_MAC0_2TOP, RK3568_GMAC0_CLKIN }
	},
	{
		RK3568_SCLK_GMAC0_RGMII_SPEED, RK3568_CRU_CLKSEL_CON(31),
		SEL(5, 4), 0,
		{ RK3568_SCLK_GMAC0, RK3568_SCLK_GMAC0,
		  RK3568_SCLK_GMAC0_DIV_50, RK3568_SCLK_GMAC0_DIV_5 }
	},
	{
		RK3568_SCLK_GMAC0_RMII_SPEED, RK3568_CRU_CLKSEL_CON(31),
		SEL(3, 3), 0,
		{ RK3568_SCLK_GMAC0_DIV_20, RK3568_SCLK_GMAC0_DIV_2 }
	},
	{
		RK3568_SCLK_GMAC0_RX_TX, RK3568_CRU_CLKSEL_CON(31),
		SEL(1, 0), 0,
		{ RK3568_SCLK_GMAC0_RGMII_SPEED, RK3568_SCLK_GMAC0_RMII_SPEED }
	},
	{
		RK3568_SCLK_GMAC1, RK3568_CRU_CLKSEL_CON(33),
		SEL(2, 2), 0,
		{ RK3568_CLK_MAC1_2TOP, RK3568_GMAC1_CLKIN }
	},
	{
		RK3568_SCLK_GMAC1_RGMII_SPEED, RK3568_CRU_CLKSEL_CON(33),
		SEL(5, 4), 0,
		{ RK3568_SCLK_GMAC1, RK3568_SCLK_GMAC1,
		  RK3568_SCLK_GMAC1_DIV_50, RK3568_SCLK_GMAC1_DIV_5 }
	},
	{
		RK3568_SCLK_GMAC1_RMII_SPEED, RK3568_CRU_CLKSEL_CON(33),
		SEL(3, 3), 0,
		{ RK3568_SCLK_GMAC1_DIV_20, RK3568_SCLK_GMAC1_DIV_2 }
	},
	{
		RK3568_SCLK_GMAC1_RX_TX, RK3568_CRU_CLKSEL_CON(33),
		SEL(1, 0), 0,
		{ RK3568_SCLK_GMAC1_RGMII_SPEED, RK3568_SCLK_GMAC1_RMII_SPEED }
	},
	{
		RK3568_CPLL_125M, RK3568_CRU_CLKSEL_CON(80),
		0, DIV(4, 0),
		{ RK3568_PLL_CPLL }
	},
	{
		RK3568_CPLL_62P5M, RK3568_CRU_CLKSEL_CON(80),
		0, DIV(12, 8),
		{ RK3568_PLL_CPLL }
	},
	{
		RK3568_CPLL_50M, RK3568_CRU_CLKSEL_CON(81),
		0, DIV(4, 0),
		{ RK3568_PLL_CPLL }
	},
	{
		RK3568_CPLL_25M, RK3568_CRU_CLKSEL_CON(81),
		0, DIV(13, 8),
		{ RK3568_PLL_CPLL }
	},
	{
		RK3568_CPLL_100M, RK3568_CRU_CLKSEL_CON(82),
		0, DIV(4, 0),
		{ RK3568_PLL_CPLL }
	},
	{
		RK3568_GPLL_400M, RK3568_CRU_CLKSEL_CON(75),
		0, DIV(4, 0),
		{ RK3568_PLL_GPLL }
	},
	{
		RK3568_GPLL_300M, RK3568_CRU_CLKSEL_CON(75),
		0, DIV(12, 8),
		{ RK3568_PLL_GPLL }
	},
	{
		RK3568_GPLL_200M, RK3568_CRU_CLKSEL_CON(76),
		0, DIV(4, 0),
		{ RK3568_PLL_GPLL }
	},
	{
		RK3568_GPLL_150M, RK3568_CRU_CLKSEL_CON(76),
		0, DIV(12, 5),
		{ RK3568_PLL_GPLL }
	},
	{
		RK3568_GPLL_100M, RK3568_CRU_CLKSEL_CON(77),
		0, DIV(4, 0),
		{ RK3568_PLL_GPLL }
	},
	{
		RK3568_CLK_OSC0_DIV_750K, RK3568_CRU_CLKSEL_CON(82),
		0, DIV(13, 8),
		{ RK3568_XIN24M }
	},
	{
		/* Sentinel */
	}
};

void
rk3568_init(struct rkclock_softc *sc)
{
	int i;

	/* The code below assumes all clocks are enabled.  Check this!. */
	for (i = 0; i <= 35; i++) {
		if (HREAD4(sc, RK3568_CRU_GATE_CON(i)) != 0x00000000) {
			printf("CRU_GATE_CON%d: 0x%08x\n", i,
			    HREAD4(sc, RK3568_CRU_GATE_CON(i)));
		}
	}

	sc->sc_clocks = rk3568_clocks;
}

int
rk3568_set_pll(struct rkclock_softc *sc, bus_size_t base, uint32_t freq)
{
	uint32_t fbdiv, postdiv1, postdiv2, refdiv;
	int mode_shift = -1;

	switch (base) {
	case RK3568_CRU_APLL_CON(0):
		mode_shift = 0;
		break;
	case RK3568_CRU_DPLL_CON(0):
		mode_shift = 2;
		break;
	case RK3568_CRU_CPLL_CON(0):
		mode_shift = 4;
		break;
	case RK3568_CRU_GPLL_CON(0):
		mode_shift = 6;
		break;
	case RK3568_CRU_NPLL_CON(0):
		mode_shift = 10;
		break;
	case RK3568_CRU_VPLL_CON(0):
		mode_shift = 12;
		break;
	}
	KASSERT(mode_shift != -1);

	/*
	 * It is not clear whether all combinations of the clock
	 * dividers result in a stable clock.  Therefore this function
	 * only supports a limited set of PLL clock rates.
	 */
	switch (freq) {
	case 1200000000U:
		postdiv1 = 2; postdiv2 = refdiv = 1;
		break;
	default:
		printf("%s: %u Hz\n", __func__, freq);
		return -1;
	}

	/* Calculate feedback divider. */
	fbdiv = freq * postdiv1 * postdiv2 * refdiv / 24000000;

	/*
	 * Select slow mode to guarantee a stable clock while we're
	 * adjusting the PLL.
	 */
	HWRITE4(sc, RK3568_CRU_MODE_CON,
	   (RK3328_CRU_CRU_MODE_MASK << 16 |
	   RK3328_CRU_CRU_MODE_SLOW) << mode_shift);

	/* Set PLL rate. */
	HWRITE4(sc, base + 0x0000,
	    RK3328_CRU_PLL_POSTDIV1_MASK << 16 |
	    postdiv1 << RK3328_CRU_PLL_POSTDIV1_SHIFT |
	    RK3328_CRU_PLL_FBDIV_MASK << 16 |
	    fbdiv << RK3328_CRU_PLL_FBDIV_SHIFT);
	HWRITE4(sc, base + 0x0004,
	    RK3328_CRU_PLL_DSMPD << 16 | RK3328_CRU_PLL_DSMPD |
	    RK3328_CRU_PLL_POSTDIV2_MASK << 16 |
	    postdiv2 << RK3328_CRU_PLL_POSTDIV2_SHIFT |
	    RK3328_CRU_PLL_REFDIV_MASK << 16 |
	    refdiv << RK3328_CRU_PLL_REFDIV_SHIFT);

	/* Wait for PLL to stabilize. */
	while ((HREAD4(sc, base + 0x0004) & RK3328_CRU_PLL_PLL_LOCK) == 0)
		delay(10);

	/* Switch back to normal mode. */
	HWRITE4(sc, RK3568_CRU_MODE_CON,
	   (RK3328_CRU_CRU_MODE_MASK << 16 |
	   RK3328_CRU_CRU_MODE_NORMAL) << mode_shift);

	return 0;
}

uint32_t
rk3568_get_frequency(void *cookie, uint32_t *cells)
{
	struct rkclock_softc *sc = cookie;
	uint32_t idx = cells[0];

	switch (idx) {
	case RK3568_PLL_APLL:
		return rk3328_get_pll(sc, RK3568_CRU_APLL_CON(0));
	case RK3568_PLL_DPLL:
		return rk3328_get_pll(sc, RK3568_CRU_DPLL_CON(0));
	case RK3568_PLL_CPLL:
		return rk3328_get_pll(sc, RK3568_CRU_CPLL_CON(0));
	case RK3568_PLL_GPLL:
		return rk3328_get_pll(sc, RK3568_CRU_GPLL_CON(0));
	case RK3568_PLL_NPLL:
		return rk3328_get_pll(sc, RK3568_CRU_NPLL_CON(0));
	case RK3568_PLL_VPLL:
		return rk3328_get_pll(sc, RK3568_CRU_VPLL_CON(0));
	case RK3568_SCLK_GMAC0_DIV_50:
		idx = RK3568_SCLK_GMAC0;
		return rk3568_get_frequency(sc, &idx) / 50;
	case RK3568_SCLK_GMAC0_DIV_5:
		idx = RK3568_SCLK_GMAC0;
		return rk3568_get_frequency(sc, &idx) / 5;
	case RK3568_SCLK_GMAC0_DIV_20:
		idx = RK3568_SCLK_GMAC0;
		return rk3568_get_frequency(sc, &idx) / 20;
	case RK3568_SCLK_GMAC0_DIV_2:
		idx = RK3568_SCLK_GMAC0;
		return rk3568_get_frequency(sc, &idx) / 2;
	case RK3568_SCLK_GMAC1_DIV_50:
		idx = RK3568_SCLK_GMAC1;
		return rk3568_get_frequency(sc, &idx) / 50;
	case RK3568_SCLK_GMAC1_DIV_5:
		idx = RK3568_SCLK_GMAC1;
		return rk3568_get_frequency(sc, &idx) / 5;
	case RK3568_SCLK_GMAC1_DIV_20:
		idx = RK3568_SCLK_GMAC1;
		return rk3568_get_frequency(sc, &idx) / 20;
	case RK3568_SCLK_GMAC1_DIV_2:
		idx = RK3568_SCLK_GMAC1;
		return rk3568_get_frequency(sc, &idx) / 2;
	case RK3568_CLK_OSC0_DIV_375K:
		idx = RK3568_CLK_OSC0_DIV_750K;
		return rk3568_get_frequency(sc, &idx) / 2;
	case RK3568_GMAC0_CLKIN:
		return rkclock_external_frequency("gmac0_clkin");
	case RK3568_GMAC1_CLKIN:
		return rkclock_external_frequency("gmac1_clkin");
	case RK3568_XIN24M:
		return 24000000;
	default:
		break;
	}

	return rkclock_get_frequency(sc, idx);
}

int
rk3568_set_frequency(void *cookie, uint32_t *cells, uint32_t freq)
{
	struct rkclock_softc *sc = cookie;
	uint32_t idx = cells[0];

	switch (idx) {
	case RK3568_PLL_GPLL:
		return rk3568_set_pll(sc, RK3568_CRU_GPLL_CON(0), freq);
	default:
		break;
	}

	return rkclock_set_frequency(sc, idx, freq);
}

int
rk3568_set_parent(void *cookie, uint32_t *cells, uint32_t *pcells)
{
	struct rkclock_softc *sc = cookie;
	char buf[64] = {};
	int len, node;

	if (pcells[0] != sc->sc_phandle) {
		node = OF_getnodebyphandle(pcells[0]);
		if (node == 0)
			return -1;
		len = OF_getproplen(node, "clock-output-names");
		if (len <= 0 || len > sizeof(buf))
			return -1;
		OF_getprop(node, "clock-output-names", buf, sizeof(buf));

		if (strcmp(buf, "gmac0_clkin") == 0) {
			return rkclock_set_parent(sc, cells[0],
			    RK3568_GMAC0_CLKIN);
		}
		if (strcmp(buf, "gmac1_clkin") == 0) {
			return rkclock_set_parent(sc, cells[0],
			    RK3568_GMAC1_CLKIN);
		}

		printf("%s: 0x%08x 0x%08x\n", __func__, cells[0], pcells[0]);
		return -1;
	}

	return rkclock_set_parent(sc, cells[0], pcells[1]);
}

void
rk3568_enable(void *cookie, uint32_t *cells, int on)
{
	uint32_t idx = cells[0];

	/* All clocks are enabled upon hardware reset. */
	if (!on) {
		printf("%s: 0x%08x\n", __func__, idx);
		return;
	}
}

void
rk3568_reset(void *cookie, uint32_t *cells, int on)
{
	struct rkclock_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t mask = (1 << (idx % 16));

	HWRITE4(sc, RK3568_CRU_SOFTRST_CON(idx / 16),
	    mask << 16 | (on ? mask : 0));
}

/* PMUCRU */

const struct rkclock rk3568_pmu_clocks[] = {
	{
		RK3568_CLK_RTC_32K, RK3568_PMUCRU_CLKSEL_CON(0),
		SEL(7, 6), 0,
		{ 0, RK3568_XIN32K, RK3568_CLK_RTC32K_FRAC },
		SET_PARENT
	},
	{
		RK3568_CLK_I2C0, RK3568_PMUCRU_CLKSEL_CON(3),
		0, DIV(15, 7),
		{ RK3568_CLK_PDPMU }
	},
	{
		RK3568_SCLK_UART0, RK3568_PMUCRU_CLKSEL_CON(4),
		SEL(11, 10), 0,
		{ 0, 0, RK3568_XIN24M }
	},
	{
		RK3568_CLK_PCIEPHY0_OSC0, 0, 0, 0,
		{ RK3568_XIN24M }
	},
	{
		RK3568_CLK_PCIEPHY0_DIV, RK3568_PMUCRU_CLKSEL_CON(9),
		0, DIV(2, 0),
		{ RK3568_PPLL_PH0 }
	},
	{
		RK3568_CLK_PCIEPHY0_REF, RK3568_PMUCRU_CLKSEL_CON(9),
		SEL(3, 3), 0,
		{ RK3568_CLK_PCIEPHY0_OSC0, RK3568_CLK_PCIEPHY0_DIV },
		SET_PARENT
	},
	{
		RK3568_CLK_PCIEPHY1_OSC0, 0, 0, 0,
		{ RK3568_XIN24M }
	},
	{
		RK3568_CLK_PCIEPHY1_DIV, RK3568_PMUCRU_CLKSEL_CON(9),
		0, DIV(6, 4),
		{ RK3568_PPLL_PH0 }
	},
	{
		RK3568_CLK_PCIEPHY1_REF, RK3568_PMUCRU_CLKSEL_CON(9),
		SEL(7, 7), 0,
		{ RK3568_CLK_PCIEPHY1_OSC0, RK3568_CLK_PCIEPHY1_DIV },
		SET_PARENT
	},
	{
		RK3568_CLK_PCIEPHY2_OSC0, 0, 0, 0,
		{ RK3568_XIN24M }
	},
	{
		RK3568_CLK_PCIEPHY2_DIV, RK3568_PMUCRU_CLKSEL_CON(9),
		0, DIV(10, 8),
		{ RK3568_PPLL_PH0 }
	},
	{
		RK3568_CLK_PCIEPHY2_REF, RK3568_PMUCRU_CLKSEL_CON(9),
		SEL(11, 11), 0,
		{ RK3568_CLK_PCIEPHY2_OSC0, RK3568_CLK_PCIEPHY2_DIV },
		SET_PARENT
	},
	{
		RK3568_CLK_PDPMU, RK3568_PMUCRU_CLKSEL_CON(2),
		SEL(15, 15), 0,
		{ RK3568_PLL_PPLL, 0 }
	},
	{
		/* Sentinel */
	}
};

void
rk3568_pmu_init(struct rkclock_softc *sc)
{
	int i;

	/* The code below assumes all clocks are enabled.  Check this!. */
	for (i = 0; i <= 2; i++) {
		if (HREAD4(sc, RK3568_PMUCRU_GATE_CON(i)) != 0x00000000) {
			printf("CRU_GATE_CON%d: 0x%08x\n", i,
			    HREAD4(sc, RK3568_CRU_GATE_CON(i)));
		}
	}

	sc->sc_clocks = rk3568_pmu_clocks;
}

int
rk3568_pmu_set_pll(struct rkclock_softc *sc, bus_size_t base, uint32_t freq)
{
	uint32_t fbdiv, postdiv1, postdiv2, refdiv;
	int mode_shift = -1;

	switch (base) {
	case RK3568_PMUCRU_PPLL_CON(0):
		mode_shift = 0;
		break;
	case RK3568_PMUCRU_HPLL_CON(0):
		mode_shift = 2;
		break;
	}
	KASSERT(mode_shift != -1);

	/*
	 * It is not clear whether all combinations of the clock
	 * dividers result in a stable clock.  Therefore this function
	 * only supports a limited set of PLL clock rates.
	 */
	switch (freq) {
	case 200000000U:
		postdiv1 = 3; postdiv2 = 4; refdiv = 1;
		break;
	default:
		printf("%s: %u Hz\n", __func__, freq);
		return -1;
	}

	/* Calculate feedback divider. */
	fbdiv = freq * postdiv1 * postdiv2 * refdiv / 24000000;

	/*
	 * Select slow mode to guarantee a stable clock while we're
	 * adjusting the PLL.
	 */
	HWRITE4(sc, RK3568_PMUCRU_MODE_CON,
	   (RK3328_CRU_CRU_MODE_MASK << 16 |
	   RK3328_CRU_CRU_MODE_SLOW) << mode_shift);

	/* Set PLL rate. */
	HWRITE4(sc, base + 0x0000,
	    RK3328_CRU_PLL_POSTDIV1_MASK << 16 |
	    postdiv1 << RK3328_CRU_PLL_POSTDIV1_SHIFT |
	    RK3328_CRU_PLL_FBDIV_MASK << 16 |
	    fbdiv << RK3328_CRU_PLL_FBDIV_SHIFT);
	HWRITE4(sc, base + 0x0004,
	    RK3328_CRU_PLL_DSMPD << 16 | RK3328_CRU_PLL_DSMPD |
	    RK3328_CRU_PLL_POSTDIV2_MASK << 16 |
	    postdiv2 << RK3328_CRU_PLL_POSTDIV2_SHIFT |
	    RK3328_CRU_PLL_REFDIV_MASK << 16 |
	    refdiv << RK3328_CRU_PLL_REFDIV_SHIFT);

	/* Wait for PLL to stabilize. */
	while ((HREAD4(sc, base + 0x0004) & RK3328_CRU_PLL_PLL_LOCK) == 0)
		delay(10);

	/* Switch back to normal mode. */
	HWRITE4(sc, RK3568_PMUCRU_MODE_CON,
	   (RK3328_CRU_CRU_MODE_MASK << 16 |
	   RK3328_CRU_CRU_MODE_NORMAL) << mode_shift);

	return 0;
}

uint32_t
rk3568_pmu_get_frequency(void *cookie, uint32_t *cells)
{
	struct rkclock_softc *sc = cookie;
	uint32_t idx = cells[0];

	switch (idx) {
	case RK3568_PLL_PPLL:
		return rk3328_get_pll(sc, RK3568_PMUCRU_PPLL_CON(0));
	case RK3568_PLL_HPLL:
		return rk3328_get_pll(sc, RK3568_PMUCRU_HPLL_CON(0));
	case RK3568_CLK_RTC32K_FRAC:
		return rk3399_get_frac(sc, RK3568_XIN24M,
		    RK3568_PMUCRU_CLKSEL_CON(1));
	case RK3568_PPLL_PH0:
		idx = RK3568_PLL_PPLL;
		return rk3568_get_frequency(sc, &idx) / 2;
	case RK3568_XIN32K:
		return 32768;
	case RK3568_XIN24M:
		return 24000000;
	default:
		break;
	}

	return rkclock_get_frequency(sc, idx);
}

int
rk3568_pmu_set_frequency(void *cookie, uint32_t *cells, uint32_t freq)
{
	struct rkclock_softc *sc = cookie;
	uint32_t idx = cells[0];

	switch (idx) {
	case RK3568_PLL_PPLL:
		return rk3568_pmu_set_pll(sc, RK3568_PMUCRU_PPLL_CON(0), freq);
	case RK3568_PLL_HPLL:
		return rk3568_pmu_set_pll(sc, RK3568_PMUCRU_HPLL_CON(0), freq);
	case RK3568_CLK_RTC32K_FRAC:
		return rk3399_set_frac(sc, RK3568_XIN24M,
		    RK3568_PMUCRU_CLKSEL_CON(1), freq);
	default:
		break;
	}

	return rkclock_set_frequency(sc, idx, freq);
}

void
rk3568_pmu_enable(void *cookie, uint32_t *cells, int on)
{
	uint32_t idx = cells[0];

	switch (idx) {
	case RK3568_CLK_USBPHY0_REF:
	case RK3568_CLK_USBPHY1_REF:
	case RK3568_CLK_PCIEPHY0_REF:
	case RK3568_CLK_PCIEPHY1_REF:
	case RK3568_CLK_PCIEPHY2_REF:
	case RK3568_CLK_PCIE30PHY_REF_M:
	case RK3568_CLK_PCIE30PHY_REF_N:
	case RK3568_CLK_I2C0:
	case RK3568_SCLK_UART0:
	case RK3568_PCLK_I2C0:
		/* Enabled by default. */
		break;
	default:
		printf("%s: 0x%08x\n", __func__, idx);
		break;
	}
}

void
rk3568_pmu_reset(void *cookie, uint32_t *cells, int on)
{
	uint32_t idx = cells[0];

	printf("%s: 0x%08x\n", __func__, idx);
}

/* 
 * Rockchip RK3588 
 */

const struct rkclock rk3588_clocks[] = {
	{
		RK3588_CLK_PWM1, RK3588_CRU_CLKSEL_CON(59),
		SEL(13, 12), 0,
		{ RK3588_CLK_100M_SRC, RK3588_CLK_50M_SRC, RK3588_XIN24M },
	},
	{
		RK3588_CLK_PWM2, RK3588_CRU_CLKSEL_CON(59),
		SEL(15, 14), 0,
		{ RK3588_CLK_100M_SRC, RK3588_CLK_50M_SRC, RK3588_XIN24M },
	},
	{
		RK3588_CLK_PWM3, RK3588_CRU_CLKSEL_CON(60),
		SEL(1, 0), 0,
		{ RK3588_CLK_100M_SRC, RK3588_CLK_50M_SRC, RK3588_XIN24M },
	},
	{
		RK3588_ACLK_BUS_ROOT, RK3588_CRU_CLKSEL_CON(38),
		SEL(5, 5), DIV(4, 0),
		{ RK3588_PLL_GPLL, RK3588_PLL_CPLL }
	},
	{
		RK3588_CLK_I2C1, RK3588_CRU_CLKSEL_CON(38),
		SEL(6, 6), 0,
		{ RK3588_CLK_200M_SRC , RK3588_CLK_100M_SRC },
	},
	{
		RK3588_CLK_I2C2, RK3588_CRU_CLKSEL_CON(38),
		SEL(7, 7), 0,
		{ RK3588_CLK_200M_SRC , RK3588_CLK_100M_SRC },
	},
	{
		RK3588_CLK_I2C3, RK3588_CRU_CLKSEL_CON(38),
		SEL(8, 8), 0,
		{ RK3588_CLK_200M_SRC , RK3588_CLK_100M_SRC },
	},
	{
		RK3588_CLK_I2C4, RK3588_CRU_CLKSEL_CON(38),
		SEL(9, 9), 0,
		{ RK3588_CLK_200M_SRC , RK3588_CLK_100M_SRC },
	},
	{
		RK3588_CLK_I2C5, RK3588_CRU_CLKSEL_CON(38),
		SEL(10, 10), 0,
		{ RK3588_CLK_200M_SRC , RK3588_CLK_100M_SRC },
	},
	{
		RK3588_CLK_I2C6, RK3588_CRU_CLKSEL_CON(38),
		SEL(11, 11), 0,
		{ RK3588_CLK_200M_SRC , RK3588_CLK_100M_SRC },
	},
	{
		RK3588_CLK_I2C7, RK3588_CRU_CLKSEL_CON(38),
		SEL(12, 12), 0,
		{ RK3588_CLK_200M_SRC , RK3588_CLK_100M_SRC },
	},
	{
		RK3588_CLK_I2C8, RK3588_CRU_CLKSEL_CON(38),
		SEL(13, 13), 0,
		{ RK3588_CLK_200M_SRC , RK3588_CLK_100M_SRC },
	},
	{
		RK3588_CLK_SPI0, RK3588_CRU_CLKSEL_CON(59),
		SEL(3, 2), 0,
		{ RK3588_CLK_200M_SRC, RK3588_CLK_150M_SRC, RK3588_XIN24M },
	},
	{
		RK3588_CLK_SPI1, RK3588_CRU_CLKSEL_CON(59),
		SEL(5, 4), 0,
		{ RK3588_CLK_200M_SRC, RK3588_CLK_150M_SRC, RK3588_XIN24M },
	},
	{
		RK3588_CLK_SPI2, RK3588_CRU_CLKSEL_CON(59),
		SEL(7, 6), 0,
		{ RK3588_CLK_200M_SRC, RK3588_CLK_150M_SRC, RK3588_XIN24M },
	},
	{
		RK3588_CLK_SPI3, RK3588_CRU_CLKSEL_CON(59),
		SEL(9, 8), 0,
		{ RK3588_CLK_200M_SRC, RK3588_CLK_150M_SRC, RK3588_XIN24M },
	},
	{
		RK3588_CLK_SPI4, RK3588_CRU_CLKSEL_CON(59),
		SEL(11, 10), 0,
		{ RK3588_CLK_200M_SRC, RK3588_CLK_150M_SRC, RK3588_XIN24M },
	},
	{
		RK3588_CLK_TSADC, RK3588_CRU_CLKSEL_CON(41),
		SEL(8, 8), DIV(7, 0),
		{ RK3588_PLL_GPLL, RK3588_XIN24M },
	},
	{
		RK3588_CLK_UART1_SRC, RK3588_CRU_CLKSEL_CON(41),
		SEL(14, 14), DIV(13, 9),
		{ RK3588_PLL_GPLL, RK3588_PLL_CPLL }
	},
	{
		RK3588_CLK_UART1, RK3588_CRU_CLKSEL_CON(43),
		SEL(1, 0), 0,
		{ RK3588_CLK_UART1_SRC, RK3588_CLK_UART1_FRAC, RK3588_XIN24M }
	},
	{
		RK3588_SCLK_UART1, 0, 0, 0,
		{ RK3588_CLK_UART1 }
	},
	{
		RK3588_CLK_UART2_SRC, RK3588_CRU_CLKSEL_CON(43),
		SEL(7, 7), DIV(6, 2),
		{ RK3588_PLL_GPLL, RK3588_PLL_CPLL }
	},
	{
		RK3588_CLK_UART2, RK3588_CRU_CLKSEL_CON(45),
		SEL(1, 0), 0,
		{ RK3588_CLK_UART2_SRC, RK3588_CLK_UART2_FRAC, RK3588_XIN24M }
	},
	{
		RK3588_SCLK_UART2, 0, 0, 0,
		{ RK3588_CLK_UART2 }
	},
	{
		RK3588_CLK_UART3_SRC, RK3588_CRU_CLKSEL_CON(45),
		SEL(7, 7), DIV(6, 2),
		{ RK3588_PLL_GPLL, RK3588_PLL_CPLL }
	},
	{
		RK3588_CLK_UART3, RK3588_CRU_CLKSEL_CON(47),
		SEL(1, 0), 0,
		{ RK3588_CLK_UART3_SRC, RK3588_CLK_UART3_FRAC, RK3588_XIN24M }
	},
	{
		RK3588_SCLK_UART3, 0, 0, 0,
		{ RK3588_CLK_UART3 }
	},
	{
		RK3588_CLK_UART4_SRC, RK3588_CRU_CLKSEL_CON(47),
		SEL(7, 7), DIV(6, 2),
		{ RK3588_PLL_GPLL, RK3588_PLL_CPLL }
	},
	{
		RK3588_CLK_UART4, RK3588_CRU_CLKSEL_CON(49),
		SEL(1, 0), 0,
		{ RK3588_CLK_UART4_SRC, RK3588_CLK_UART4_FRAC, RK3588_XIN24M }
	},
	{
		RK3588_SCLK_UART4, 0, 0, 0,
		{ RK3588_CLK_UART4 }
	},
	{
		RK3588_CLK_UART5_SRC, RK3588_CRU_CLKSEL_CON(49),
		SEL(7, 7), DIV(6, 2),
		{ RK3588_PLL_GPLL, RK3588_PLL_CPLL }
	},
	{
		RK3588_CLK_UART5, RK3588_CRU_CLKSEL_CON(51),
		SEL(1, 0), 0,
		{ RK3588_CLK_UART5_SRC, RK3588_CLK_UART5_FRAC, RK3588_XIN24M }
	},
	{
		RK3588_SCLK_UART5, 0, 0, 0,
		{ RK3588_CLK_UART5 }
	},
	{
		RK3588_CLK_UART6_SRC, RK3588_CRU_CLKSEL_CON(51),
		SEL(7, 7), DIV(6, 2),
		{ RK3588_PLL_GPLL, RK3588_PLL_CPLL }
	},
	{
		RK3588_CLK_UART6, RK3588_CRU_CLKSEL_CON(53),
		SEL(1, 0), 0,
		{ RK3588_CLK_UART6_SRC, RK3588_CLK_UART6_FRAC, RK3588_XIN24M }
	},
	{
		RK3588_SCLK_UART6, 0, 0, 0,
		{ RK3588_CLK_UART6 }
	},
	{
		RK3588_CLK_UART7_SRC, RK3588_CRU_CLKSEL_CON(53),
		SEL(7, 7), DIV(6, 2),
		{ RK3588_PLL_GPLL, RK3588_PLL_CPLL }
	},
	{
		RK3588_CLK_UART7, RK3588_CRU_CLKSEL_CON(55),
		SEL(1, 0), 0,
		{ RK3588_CLK_UART7_SRC, RK3588_CLK_UART7_FRAC, RK3588_XIN24M }
	},
	{
		RK3588_SCLK_UART7, 0, 0, 0,
		{ RK3588_CLK_UART7 }
	},
	{
		RK3588_CLK_UART8_SRC, RK3588_CRU_CLKSEL_CON(55),
		SEL(7, 7), DIV(6, 2),
		{ RK3588_PLL_GPLL, RK3588_PLL_CPLL }
	},
	{
		RK3588_CLK_UART8, RK3588_CRU_CLKSEL_CON(57),
		SEL(1, 0), 0,
		{ RK3588_CLK_UART8_SRC, RK3588_CLK_UART8_FRAC, RK3588_XIN24M }
	},
	{
		RK3588_SCLK_UART8, 0, 0, 0,
		{ RK3588_CLK_UART8 }
	},
	{
		RK3588_CLK_UART9_SRC, RK3588_CRU_CLKSEL_CON(57),
		SEL(7, 7), DIV(6, 2),
		{ RK3588_PLL_GPLL, RK3588_PLL_CPLL }
	},
	{
		RK3588_CLK_UART9, RK3588_CRU_CLKSEL_CON(59),
		SEL(1, 0), 0,
		{ RK3588_CLK_UART9_SRC, RK3588_CLK_UART9_FRAC, RK3588_XIN24M }
	},
	{
		RK3588_SCLK_UART9, 0, 0, 0,
		{ RK3588_CLK_UART9 }
	},
	{
		RK3588_ACLK_CENTER_ROOT, RK3588_CRU_CLKSEL_CON(165),
		SEL(1, 0), 0,
		{ RK3588_CLK_700M_SRC, RK3588_CLK_400M_SRC,
		  RK3588_CLK_200M_SRC, RK3588_XIN24M }
	},
	{
		RK3588_ACLK_CENTER_LOW_ROOT, RK3588_CRU_CLKSEL_CON(165),
		SEL(3, 2), 0,
		{ RK3588_CLK_500M_SRC, RK3588_CLK_250M_SRC,
		  RK3588_CLK_100M_SRC, RK3588_XIN24M }
	},
	{
		RK3588_HCLK_CENTER_ROOT, RK3588_CRU_CLKSEL_CON(165),
		SEL(5, 4), 0,
		{ RK3588_CLK_400M_SRC, RK3588_CLK_200M_SRC,
		  RK3588_CLK_100M_SRC, RK3588_XIN24M }
	},
	{
		RK3588_CLK_50M_SRC, RK3588_CRU_CLKSEL_CON(0),
		SEL(5, 5), DIV(4, 0),
		{ RK3588_PLL_GPLL, RK3588_PLL_CPLL }
	},
	{
		RK3588_CLK_100M_SRC, RK3588_CRU_CLKSEL_CON(0),
		SEL(11, 11), DIV(10, 6),
		{ RK3588_PLL_GPLL, RK3588_PLL_CPLL }
	},
	{
		RK3588_CLK_150M_SRC, RK3588_CRU_CLKSEL_CON(1),
		SEL(5, 5), DIV(4, 0),
		{ RK3588_PLL_GPLL, RK3588_PLL_CPLL }
	},
	{
		RK3588_CLK_200M_SRC, RK3588_CRU_CLKSEL_CON(1),
		SEL(11, 11), DIV(10, 6),
		{ RK3588_PLL_GPLL, RK3588_PLL_CPLL }
	},
	{
		RK3588_CLK_250M_SRC, RK3588_CRU_CLKSEL_CON(2),
		SEL(5, 5), DIV(4, 0),
		{ RK3588_PLL_GPLL, RK3588_PLL_CPLL }
	},
	{
		RK3588_CLK_400M_SRC, RK3588_CRU_CLKSEL_CON(3),
		SEL(11, 11), DIV(10, 6),
		{ RK3588_PLL_GPLL, RK3588_PLL_CPLL }
	},
	{
		RK3588_CLK_500M_SRC, RK3588_CRU_CLKSEL_CON(4),
		SEL(11, 11), DIV(10, 6),
		{ RK3588_PLL_GPLL, RK3588_PLL_CPLL }
	},
	{
		RK3588_CLK_700M_SRC, RK3588_CRU_CLKSEL_CON(6),
		SEL(5, 5), DIV(4, 0),
		{ RK3588_PLL_GPLL, RK3588_PLL_SPLL }
	},
	{
		RK3588_ACLK_TOP_ROOT, RK3588_CRU_CLKSEL_CON(8),
		SEL(6, 5), 0,
		{ RK3588_PLL_GPLL, RK3588_PLL_CPLL, RK3588_PLL_AUPLL }
	},
	{
		RK3588_PCLK_TOP_ROOT, RK3588_CRU_CLKSEL_CON(8),
		SEL(8, 7), 0,
		{ RK3588_CLK_100M_SRC, RK3588_CLK_50M_SRC, RK3588_XIN24M }
	},
	{
		RK3588_ACLK_LOW_TOP_ROOT, RK3588_CRU_CLKSEL_CON(8),
		SEL(14, 14), DIV(13, 9),
		{ RK3588_PLL_GPLL, RK3588_PLL_CPLL }
	},
	{
		RK3588_CLK_GPU_SRC, RK3588_CRU_CLKSEL_CON(158),
		SEL(7, 5), DIV(4, 0),
		{ RK3588_PLL_GPLL, RK3588_PLL_CPLL, RK3588_PLL_AUPLL,
		  RK3588_PLL_NPLL, RK3588_PLL_SPLL }
	},
	{
		RK3588_CLK_GPU, 0, 0, 0,
		{ RK3588_CLK_GPU_SRC },
		SET_PARENT
	},
	{
		RK3588_CCLK_EMMC, RK3588_CRU_CLKSEL_CON(77),
		SEL(15, 14), DIV(13, 8),
		{ RK3588_PLL_GPLL, RK3588_PLL_CPLL, RK3588_XIN24M }
	},
	{
		RK3588_BCLK_EMMC, RK3588_CRU_CLKSEL_CON(78),
		SEL(5, 5), DIV(4, 0),
		{ RK3588_PLL_GPLL, RK3588_PLL_CPLL }
	},
	{
		RK3588_TMCLK_EMMC, 0, 0, 0,
		{ RK3588_XIN24M }
	},
	{
		RK3588_CLK_GMAC_125M, RK3588_CRU_CLKSEL_CON(83),
		SEL(15, 15), DIV(14, 8),
		{ RK3588_PLL_GPLL, RK3588_PLL_CPLL }
	},
	{
		RK3588_CCLK_SRC_SDIO, RK3588_CRU_CLKSEL_CON(172),
		SEL(9, 8), DIV(7, 2),
		{ RK3588_PLL_GPLL, RK3588_PLL_CPLL, RK3588_XIN24M }
	},
	{
		RK3588_ACLK_VOP_ROOT, RK3588_CRU_CLKSEL_CON(110),
		SEL(7, 5), DIV(4, 0),
		{ RK3588_PLL_GPLL, RK3588_PLL_CPLL, RK3588_PLL_AUPLL,
		  RK3588_PLL_NPLL, RK3588_PLL_SPLL }
	},
	{
		RK3588_ACLK_VOP, 0, 0, 0,
		{ RK3588_ACLK_VOP_SUB_SRC },
		SET_PARENT
	},
	{
		RK3588_ACLK_VOP_SUB_SRC, RK3588_CRU_CLKSEL_CON(115),
		SEL(9, 9), 0,
		{ RK3588_ACLK_VOP_ROOT, 0 /* RK3588_ACLK_VOP_DIV2_SRC */ },
		SET_PARENT
	},
	{
		RK3588_CLK_I2C0, RK3588_CRU_CLKSEL_CON(3),
		SEL(6, 6), 0,
		{ RK3588_CLK_PMU1_200M_SRC, RK3588_CLK_PMU1_100M_SRC },
	},
	{
		RK3588_CLK_PMU1_50M_SRC, RK3588_PMUCRU_CLKSEL_CON(0),
		0, DIV(3, 0),
		{ RK3588_CLK_PMU1_400M_SRC }
	},
	{
		RK3588_CLK_PMU1_100M_SRC, RK3588_PMUCRU_CLKSEL_CON(0),
		0, DIV(6, 4),
		{ RK3588_CLK_PMU1_400M_SRC }
	},
	{
		RK3588_CLK_PMU1_200M_SRC, RK3588_PMUCRU_CLKSEL_CON(0),
		0, DIV(9, 7),
		{ RK3588_CLK_PMU1_400M_SRC }
	},
	{
		RK3588_CLK_PMU1_400M_SRC, RK3588_PMUCRU_CLKSEL_CON(1),
		SEL(5, 5), DIV(4, 0),
		{ RK3588_CLK_400M_SRC, RK3588_XIN24M }
	},
	{
		RK3588_PCLK_PMU1_ROOT, RK3588_PMUCRU_CLKSEL_CON(1),
		SEL(9, 8), 0,
		{ RK3588_CLK_PMU1_100M_SRC, RK3588_CLK_PMU1_50M_SRC,
		  RK3588_XIN24M }
	},
	{
		RK3588_PCLK_PMU0_ROOT, 0, 0, 0,
		{ RK3588_PCLK_PMU1_ROOT },
		SET_PARENT
	},
	{
		RK3588_HCLK_PMU_CM0_ROOT, RK3588_PMUCRU_CLKSEL_CON(1),
		SEL(11, 10), 0,
		{ RK3588_CLK_PMU1_400M_SRC, RK3588_CLK_PMU1_200M_SRC,
		  RK3588_CLK_PMU1_100M_SRC, RK3588_XIN24M }
	},
	{
		RK3588_CLK_PMU1PWM, RK3588_PMUCRU_CLKSEL_CON(2),
		SEL(10, 9), 0,
		{ RK3588_CLK_PMU1_100M_SRC, RK3588_CLK_PMU1_50M_SRC,
		  RK3588_XIN24M }
	},
	{
		RK3588_CLK_UART0_SRC, RK3588_PMUCRU_CLKSEL_CON(3),
		0, DIV(11, 7),
		{ RK3588_PLL_CPLL }
	},
	{
		RK3588_CLK_UART0, RK3588_PMUCRU_CLKSEL_CON(5),
		SEL(1, 0), 0,
		{ RK3588_CLK_UART0_SRC, RK3588_CLK_UART0_FRAC, RK3588_XIN24M }
	},
	{
		RK3588_SCLK_UART0, 0, 0, 0,
		{ RK3588_CLK_UART0 }
	},
	{
		RK3588_CLK_REF_PIPE_PHY0_OSC_SRC, 0, 0, 0,
		{ RK3588_XIN24M }
	},
	{
		RK3588_CLK_REF_PIPE_PHY1_OSC_SRC, 0, 0, 0,
		{ RK3588_XIN24M }
	},
	{
		RK3588_CLK_REF_PIPE_PHY2_OSC_SRC, 0, 0, 0,
		{ RK3588_XIN24M }
	},
	{
		RK3588_CLK_REF_PIPE_PHY0_PLL_SRC, RK3588_CRU_CLKSEL_CON(176),
		0, DIV(5, 0),
		{ RK3588_PLL_PPLL }
	},
	{
		RK3588_CLK_REF_PIPE_PHY1_PLL_SRC, RK3588_CRU_CLKSEL_CON(176),
		0, DIV(11, 6),
		{ RK3588_PLL_PPLL }
	},
	{
		RK3588_CLK_REF_PIPE_PHY2_PLL_SRC, RK3588_CRU_CLKSEL_CON(177),
		0, DIV(5, 0),
		{ RK3588_PLL_PPLL }
	},
	{
		RK3588_CLK_REF_PIPE_PHY0, RK3588_CRU_CLKSEL_CON(177),
		SEL(6, 6), 0,
		{ RK3588_CLK_REF_PIPE_PHY0_OSC_SRC,
		  RK3588_CLK_REF_PIPE_PHY0_PLL_SRC },
	},
	{
		RK3588_CLK_REF_PIPE_PHY1, RK3588_CRU_CLKSEL_CON(177),
		SEL(7, 7), 0,
		{ RK3588_CLK_REF_PIPE_PHY1_OSC_SRC,
		  RK3588_CLK_REF_PIPE_PHY1_PLL_SRC },
	},
	{
		RK3588_CLK_REF_PIPE_PHY2, RK3588_CRU_CLKSEL_CON(177),
		SEL(8, 8), 0,
		{ RK3588_CLK_REF_PIPE_PHY2_OSC_SRC,
		  RK3588_CLK_REF_PIPE_PHY2_PLL_SRC },
	},
	{
		/* Sentinel */
	}
};

/* Certain test clocks are disabled. */
const uint32_t rk3588_gates[78] = {
	[2] = 0x00000050,
	[22] = 0x00000200,
	[25] = 0x00000200,
	[29] = 0x00000004,
	[66] = 0x00000004,
};

void
rk3588_init(struct rkclock_softc *sc)
{
	int i;

	/* The code below assumes all clocks are enabled.  Check this!. */
	for (i = 0; i < nitems(rk3588_gates); i++) {
		if (HREAD4(sc, RK3588_CRU_GATE_CON(i)) != rk3588_gates[i]) {
			printf("CRU_GATE_CON%d: 0x%08x\n", i,
			    HREAD4(sc, RK3588_CRU_GATE_CON(i)));
		}
	}

	sc->sc_clocks = rk3588_clocks;
}

int
rk3588_set_pll(struct rkclock_softc *sc, bus_size_t base, uint32_t freq)
{
	uint32_t p, m, s, k;
	int mode_shift = -1;

	switch (base) {
	case RK3588_CRU_AUPLL_CON(0):
		mode_shift = 6;
		break;
	case RK3588_CRU_GPLL_CON(0):
		mode_shift = 2;
		break;
	case RK3588_CRU_NPLL_CON(0):
		mode_shift = 0;
		break;
	case RK3588_PHPTOPCRU_PPLL_CON(0):
		mode_shift = 10;
		break;
	}
	KASSERT(mode_shift != -1);

	/*
	 * It is not clear whether all combinations of the clock
	 * dividers result in a stable clock.  Therefore this function
	 * only supports a limited set of PLL clock rates.
	 */
	switch (freq) {
	case 1188000000U:
		p = 2; m = 198; s = 1; k = 0;
		break;
	case 1100000000U:
		p = 3; m = 550; s = 2; k = 0;
		break;
	case 850000000U:
		p = 3; m = 425; s = 2; k = 0;
		break;
	case 786432000U:
		p = 2; m = 262; s = 2; k = 9437;
		break;
	case 100000000U:
		p = 3; m = 400; s = 5; k = 0;
		break;
	default:
		printf("%s: %u Hz\n", __func__, freq);
		return -1;
	}

	/*
	 * Select slow mode to guarantee a stable clock while we're
	 * adjusting the PLL.
	 */
	HWRITE4(sc, RK3588_CRU_MODE_CON,
	    (RK3588_CRU_MODE_MASK << 16 |RK3588_CRU_MODE_SLOW) << mode_shift);

	/* Power down PLL. */
	HWRITE4(sc, base + 0x0004,
	    RK3588_CRU_PLL_RESETB << 16 | RK3588_CRU_PLL_RESETB);
	
	/* Set PLL rate. */
	HWRITE4(sc, base + 0x0000,
	    RK3588_CRU_PLL_M_MASK << 16 | m << RK3588_CRU_PLL_M_SHIFT);
	HWRITE4(sc, base + 0x0004,
	    RK3588_CRU_PLL_S_MASK << 16 | s << RK3588_CRU_PLL_S_SHIFT |
	    RK3588_CRU_PLL_P_MASK << 16 | p << RK3588_CRU_PLL_P_SHIFT);
	HWRITE4(sc, base + 0x0008,
	    RK3588_CRU_PLL_K_MASK << 16 | k << RK3588_CRU_PLL_K_SHIFT);

	/* Power up PLL. */
	HWRITE4(sc, base + 0x0004, RK3588_CRU_PLL_RESETB << 16);

	/* Wait for PLL to stabilize. */
	while ((HREAD4(sc, base + 0x0018) & RK3588_CRU_PLL_PLL_LOCK) == 0)
		delay(10);

	/* Switch back to normal mode. */
	HWRITE4(sc, RK3588_CRU_MODE_CON,
	    (RK3588_CRU_MODE_MASK << 16 | RK3588_CRU_MODE_NORMAL) << mode_shift);

	return 0;
}

uint32_t
rk3588_get_pll(struct rkclock_softc *sc, bus_size_t base)
{
	uint64_t freq, frac;
	uint32_t k, m, p, s;
	uint32_t reg;

	reg = HREAD4(sc, base);
	m = (reg & RK3588_CRU_PLL_M_MASK) >> RK3588_CRU_PLL_M_SHIFT;
	reg = HREAD4(sc, base + 4);
	p = (reg & RK3588_CRU_PLL_P_MASK) >> RK3588_CRU_PLL_P_SHIFT;
	s = (reg & RK3588_CRU_PLL_S_MASK) >> RK3588_CRU_PLL_S_SHIFT;
	reg = HREAD4(sc, base + 8);
	k = (reg & RK3588_CRU_PLL_K_MASK) >> RK3588_CRU_PLL_K_SHIFT;

	freq = (24000000ULL * m) / p;
	if (k) {
		frac = ((24000000ULL * k) / (p * 65535));
		freq += frac;
	}

	return freq >> s;
}

uint32_t
rk3588_get_frequency(void *cookie, uint32_t *cells)
{
	struct rkclock_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t freq;

	switch (idx) {
	case RK3588_PLL_AUPLL:
		return rk3588_get_pll(sc, RK3588_CRU_AUPLL_CON(0));
	case RK3588_PLL_CPLL:
		return rk3588_get_pll(sc, RK3588_CRU_CPLL_CON(0));
	case RK3588_PLL_GPLL:
		return rk3588_get_pll(sc, RK3588_CRU_GPLL_CON(0));
	case RK3588_PLL_NPLL:
		return rk3588_get_pll(sc, RK3588_CRU_NPLL_CON(0));
	case RK3588_PLL_PPLL:
		return rk3588_get_pll(sc, RK3588_PHPTOPCRU_PPLL_CON(0));
	case RK3588_PLL_SPLL:
		return rkclock_external_frequency("spll");
	case RK3588_XIN24M:
		return 24000000;
	default:
		break;
	}

	freq = rkclock_get_frequency(sc, idx);
	return freq;
}

int
rk3588_set_frequency(void *cookie, uint32_t *cells, uint32_t freq)
{
	struct rkclock_softc *sc = cookie;
	uint32_t idx = cells[0];

	switch (idx) {
	case RK3588_PLL_AUPLL:
		return rk3588_set_pll(sc, RK3588_CRU_AUPLL_CON(0), freq);
	case RK3588_PLL_GPLL:
		return rk3588_set_pll(sc, RK3588_CRU_GPLL_CON(0), freq);
	case RK3588_PLL_NPLL:
		return rk3588_set_pll(sc, RK3588_CRU_NPLL_CON(0), freq);
	case RK3588_PLL_PPLL:
		return rk3588_set_pll(sc, RK3588_PHPTOPCRU_PPLL_CON(0), freq);
	default:
		break;
	}

	return rkclock_set_frequency(sc, idx, freq);
}

void
rk3588_enable(void *cookie, uint32_t *cells, int on)
{
	uint32_t idx = cells[0];

	/* All clocks are enabled upon hardware reset. */
	if (!on) {
		printf("%s: 0x%08x\n", __func__, idx);
		return;
	}
}

void
rk3588_reset(void *cookie, uint32_t *cells, int on)
{
	struct rkclock_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t bit, mask, reg;

	switch (idx) {
	case RK3588_SRST_P_TSADC:
		reg = RK3588_CRU_SOFTRST_CON(12);
		bit = 0;
		break;
	case RK3588_SRST_TSADC:
		reg = RK3588_CRU_SOFTRST_CON(12);
		bit = 1;
		break;
	case RK3588_SRST_H_EMMC:
		reg = RK3588_CRU_SOFTRST_CON(31);
		bit = 4;
		break;
	case RK3588_SRST_A_EMMC:
		reg = RK3588_CRU_SOFTRST_CON(31);
		bit = 5;
		break;
	case RK3588_SRST_C_EMMC:
		reg = RK3588_CRU_SOFTRST_CON(31);
		bit = 6;
		break;
	case RK3588_SRST_B_EMMC:
		reg = RK3588_CRU_SOFTRST_CON(31);
		bit = 7;
		break;
	case RK3588_SRST_T_EMMC:
		reg = RK3588_CRU_SOFTRST_CON(31);
		bit = 8;
		break;
	case RK3588_SRST_A_GMAC0:
		reg = RK3588_CRU_SOFTRST_CON(32);
		bit = 10;
		break;
	case RK3588_SRST_A_GMAC1:
		reg = RK3588_CRU_SOFTRST_CON(32);
		bit = 11;
		break;
	case RK3588_SRST_PCIE0_POWER_UP:
		reg = RK3588_CRU_SOFTRST_CON(32);
		bit = 13;
		break;
	case RK3588_SRST_PCIE1_POWER_UP:
		reg = RK3588_CRU_SOFTRST_CON(32);
		bit = 14;
		break;
	case RK3588_SRST_PCIE2_POWER_UP:
		reg = RK3588_CRU_SOFTRST_CON(32);
		bit = 15;
		break;
	case RK3588_SRST_PCIE3_POWER_UP:
		reg = RK3588_CRU_SOFTRST_CON(33);
		bit = 0;
		break;
	case RK3588_SRST_PCIE4_POWER_UP:
		reg = RK3588_CRU_SOFTRST_CON(33);
		bit = 1;
		break;
	case RK3588_SRST_P_PCIE0:
		reg = RK3588_CRU_SOFTRST_CON(33);
		bit = 12;
		break;
	case RK3588_SRST_P_PCIE1:
		reg = RK3588_CRU_SOFTRST_CON(33);
		bit = 13;
		break;
	case RK3588_SRST_P_PCIE2:
		reg = RK3588_CRU_SOFTRST_CON(33);
		bit = 14;
		break;
	case RK3588_SRST_P_PCIE3:
		reg = RK3588_CRU_SOFTRST_CON(33);
		bit = 15;
		break;
	case RK3588_SRST_P_PCIE4:
		reg = RK3588_CRU_SOFTRST_CON(34);
		bit = 0;
		break;
	case RK3588_SRST_A_USB3OTG2:
		reg = RK3588_CRU_SOFTRST_CON(35);
		bit = 7;
		break;
	case RK3588_SRST_A_USB3OTG0:
		reg = RK3588_CRU_SOFTRST_CON(42);
		bit = 4;
		break;
	case RK3588_SRST_A_USB3OTG1:
		reg = RK3588_CRU_SOFTRST_CON(42);
		bit = 7;
		break;
	case RK3588_SRST_REF_PIPE_PHY0:
		reg = RK3588_CRU_SOFTRST_CON(77);
		bit = 6;
		break;
	case RK3588_SRST_REF_PIPE_PHY1:
		reg = RK3588_CRU_SOFTRST_CON(77);
		bit = 7;
		break;
	case RK3588_SRST_REF_PIPE_PHY2:
		reg = RK3588_CRU_SOFTRST_CON(77);
		bit = 8;
		break;
	case RK3588_SRST_P_PCIE2_PHY0:
		reg = RK3588_PHPTOPCRU_SOFTRST_CON(0);
		bit = 5;
		break;
	case RK3588_SRST_P_PCIE2_PHY1:
		reg = RK3588_PHPTOPCRU_SOFTRST_CON(0);
		bit = 6;
		break;
	case RK3588_SRST_P_PCIE2_PHY2:
		reg = RK3588_PHPTOPCRU_SOFTRST_CON(0);
		bit = 7;
		break;
	case RK3588_SRST_PCIE30_PHY:
		reg = RK3588_PHPTOPCRU_SOFTRST_CON(0);
		bit = 10;
		break;
	default:
		printf("%s: 0x%08x\n", __func__, idx);
		return;
	}

	mask = (1 << bit);
	HWRITE4(sc, reg, mask << 16 | (on ? mask : 0));
}
