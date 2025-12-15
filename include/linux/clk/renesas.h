/* SPDX-License-Identifier: GPL-2.0+
 *
 * Copyright 2013 Ideas On Board SPRL
 * Copyright 2013, 2014 Horms Solutions Ltd.
 *
 * Contact: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 * Contact: Simon Horman <horms@verge.net.au>
 */

#ifndef __LINUX_CLK_RENESAS_H_
#define __LINUX_CLK_RENESAS_H_

#include <linux/clk-provider.h>
#include <linux/types.h>
#include <linux/units.h>

struct device;
struct device_node;
struct generic_pm_domain;

void cpg_mstp_add_clk_domain(struct device_node *np);
#ifdef CONFIG_CLK_RENESAS_CPG_MSTP
int cpg_mstp_attach_dev(struct generic_pm_domain *unused, struct device *dev);
void cpg_mstp_detach_dev(struct generic_pm_domain *unused, struct device *dev);
#else
#define cpg_mstp_attach_dev	NULL
#define cpg_mstp_detach_dev	NULL
#endif

#ifdef CONFIG_CLK_RENESAS_CPG_MSSR
int cpg_mssr_attach_dev(struct generic_pm_domain *unused, struct device *dev);
void cpg_mssr_detach_dev(struct generic_pm_domain *unused, struct device *dev);
#else
#define cpg_mssr_attach_dev	NULL
#define cpg_mssr_detach_dev	NULL
#endif

/**
 * struct rzv2h_pll_limits - PLL parameter constraints
 *
 * This structure defines the minimum and maximum allowed values for
 * various parameters used to configure a PLL. These limits ensure
 * the PLL operates within valid and stable ranges.
 *
 * @fout: Output frequency range (in MHz)
 * @fout.min: Minimum allowed output frequency
 * @fout.max: Maximum allowed output frequency
 *
 * @fvco: PLL oscillation frequency range (in MHz)
 * @fvco.min: Minimum allowed VCO frequency
 * @fvco.max: Maximum allowed VCO frequency
 *
 * @m: Main-divider range
 * @m.min: Minimum main-divider value
 * @m.max: Maximum main-divider value
 *
 * @p: Pre-divider range
 * @p.min: Minimum pre-divider value
 * @p.max: Maximum pre-divider value
 *
 * @s: Divider range
 * @s.min: Minimum divider value
 * @s.max: Maximum divider value
 *
 * @k: Delta-sigma modulator range (signed)
 * @k.min: Minimum delta-sigma value
 * @k.max: Maximum delta-sigma value
 */
struct rzv2h_pll_limits {
	struct {
		u32 min;
		u32 max;
	} fout;

	struct {
		u32 min;
		u32 max;
	} fvco;

	struct {
		u16 min;
		u16 max;
	} m;

	struct {
		u8 min;
		u8 max;
	} p;

	struct {
		u8 min;
		u8 max;
	} s;

	struct {
		s16 min;
		s16 max;
	} k;
};

/**
 * struct rzv2h_pll_pars - PLL configuration parameters
 *
 * This structure contains the configuration parameters for the
 * Phase-Locked Loop (PLL), used to achieve a specific output frequency.
 *
 * @m: Main divider value
 * @p: Pre-divider value
 * @s: Output divider value
 * @k: Delta-sigma modulation value
 * @freq_millihz: Calculated PLL output frequency in millihertz
 * @error_millihz: Frequency error from target in millihertz (signed)
 */
struct rzv2h_pll_pars {
	u16 m;
	u8 p;
	u8 s;
	s16 k;
	u64 freq_millihz;
	s64 error_millihz;
};

/**
 * struct rzv2h_pll_div_pars - PLL parameters with post-divider
 *
 * This structure is used for PLLs that include an additional post-divider
 * stage after the main PLL block. It contains both the PLL configuration
 * parameters and the resulting frequency/error values after the divider.
 *
 * @pll: Main PLL configuration parameters (see struct rzv2h_pll_pars)
 *
 * @div: Post-divider configuration and result
 * @div.divider_value: Divider applied to the PLL output
 * @div.freq_millihz: Output frequency after divider in millihertz
 * @div.error_millihz: Frequency error from target in millihertz (signed)
 */
struct rzv2h_pll_div_pars {
	struct rzv2h_pll_pars pll;
	struct {
		u8 divider_value;
		u64 freq_millihz;
		s64 error_millihz;
	} div;
};

#define RZV2H_CPG_PLL_DSI_LIMITS(name)					\
	static const struct rzv2h_pll_limits (name) = {			\
		.fout = { .min = 25 * MEGA, .max = 375 * MEGA },	\
		.fvco = { .min = 1600 * MEGA, .max = 3200 * MEGA },	\
		.m = { .min = 64, .max = 533 },				\
		.p = { .min = 1, .max = 4 },				\
		.s = { .min = 0, .max = 6 },				\
		.k = { .min = -32768, .max = 32767 },			\
	}								\

#ifdef CONFIG_CLK_RZV2H
bool rzv2h_get_pll_pars(const struct rzv2h_pll_limits *limits,
			struct rzv2h_pll_pars *pars, u64 freq_millihz);

bool rzv2h_get_pll_divs_pars(const struct rzv2h_pll_limits *limits,
			     struct rzv2h_pll_div_pars *pars,
			     const u8 *table, u8 table_size, u64 freq_millihz);
#else
static inline bool rzv2h_get_pll_pars(const struct rzv2h_pll_limits *limits,
				      struct rzv2h_pll_pars *pars,
				      u64 freq_millihz)
{
	return false;
}

static inline bool rzv2h_get_pll_divs_pars(const struct rzv2h_pll_limits *limits,
					   struct rzv2h_pll_div_pars *pars,
					   const u8 *table, u8 table_size,
					   u64 freq_millihz)
{
	return false;
}
#endif

#endif
