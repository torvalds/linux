/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Cadence Design Systems Inc.
 */

#ifndef __PHY_MIPI_DPHY_H_
#define __PHY_MIPI_DPHY_H_

/**
 * struct phy_configure_opts_mipi_dphy - MIPI D-PHY configuration set
 *
 * This structure is used to represent the configuration state of a
 * MIPI D-PHY phy.
 */
struct phy_configure_opts_mipi_dphy {
	/**
	 * @clk_miss:
	 *
	 * Timeout, in picoseconds, for receiver to detect absence of
	 * Clock transitions and disable the Clock Lane HS-RX.
	 *
	 * Maximum value: 60000 ps
	 */
	unsigned int		clk_miss;

	/**
	 * @clk_post:
	 *
	 * Time, in picoseconds, that the transmitter continues to
	 * send HS clock after the last associated Data Lane has
	 * transitioned to LP Mode. Interval is defined as the period
	 * from the end of @hs_trail to the beginning of @clk_trail.
	 *
	 * Minimum value: 60000 ps + 52 * @hs_clk_rate period in ps
	 */
	unsigned int		clk_post;

	/**
	 * @clk_pre:
	 *
	 * Time, in UI, that the HS clock shall be driven by
	 * the transmitter prior to any associated Data Lane beginning
	 * the transition from LP to HS mode.
	 *
	 * Minimum value: 8 UI
	 */
	unsigned int		clk_pre;

	/**
	 * @clk_prepare:
	 *
	 * Time, in picoseconds, that the transmitter drives the Clock
	 * Lane LP-00 Line state immediately before the HS-0 Line
	 * state starting the HS transmission.
	 *
	 * Minimum value: 38000 ps
	 * Maximum value: 95000 ps
	 */
	unsigned int		clk_prepare;

	/**
	 * @clk_settle:
	 *
	 * Time interval, in picoseconds, during which the HS receiver
	 * should ignore any Clock Lane HS transitions, starting from
	 * the beginning of @clk_prepare.
	 *
	 * Minimum value: 95000 ps
	 * Maximum value: 300000 ps
	 */
	unsigned int		clk_settle;

	/**
	 * @clk_term_en:
	 *
	 * Time, in picoseconds, for the Clock Lane receiver to enable
	 * the HS line termination.
	 *
	 * Maximum value: 38000 ps
	 */
	unsigned int		clk_term_en;

	/**
	 * @clk_trail:
	 *
	 * Time, in picoseconds, that the transmitter drives the HS-0
	 * state after the last payload clock bit of a HS transmission
	 * burst.
	 *
	 * Minimum value: 60000 ps
	 */
	unsigned int		clk_trail;

	/**
	 * @clk_zero:
	 *
	 * Time, in picoseconds, that the transmitter drives the HS-0
	 * state prior to starting the Clock.
	 */
	unsigned int		clk_zero;

	/**
	 * @d_term_en:
	 *
	 * Time, in picoseconds, for the Data Lane receiver to enable
	 * the HS line termination.
	 *
	 * Maximum value: 35000 ps + 4 * @hs_clk_rate period in ps
	 */
	unsigned int		d_term_en;

	/**
	 * @eot:
	 *
	 * Transmitted time interval, in picoseconds, from the start
	 * of @hs_trail or @clk_trail, to the start of the LP- 11
	 * state following a HS burst.
	 *
	 * Maximum value: 105000 ps + 12 * @hs_clk_rate period in ps
	 */
	unsigned int		eot;

	/**
	 * @hs_exit:
	 *
	 * Time, in picoseconds, that the transmitter drives LP-11
	 * following a HS burst.
	 *
	 * Minimum value: 100000 ps
	 */
	unsigned int		hs_exit;

	/**
	 * @hs_prepare:
	 *
	 * Time, in picoseconds, that the transmitter drives the Data
	 * Lane LP-00 Line state immediately before the HS-0 Line
	 * state starting the HS transmission.
	 *
	 * Minimum value: 40000 ps + 4 * @hs_clk_rate period in ps
	 * Maximum value: 85000 ps + 6 * @hs_clk_rate period in ps
	 */
	unsigned int		hs_prepare;

	/**
	 * @hs_settle:
	 *
	 * Time interval, in picoseconds, during which the HS receiver
	 * shall ignore any Data Lane HS transitions, starting from
	 * the beginning of @hs_prepare.
	 *
	 * Minimum value: 85000 ps + 6 * @hs_clk_rate period in ps
	 * Maximum value: 145000 ps + 10 * @hs_clk_rate period in ps
	 */
	unsigned int		hs_settle;

	/**
	 * @hs_skip:
	 *
	 * Time interval, in picoseconds, during which the HS-RX
	 * should ignore any transitions on the Data Lane, following a
	 * HS burst. The end point of the interval is defined as the
	 * beginning of the LP-11 state following the HS burst.
	 *
	 * Minimum value: 40000 ps
	 * Maximum value: 55000 ps + 4 * @hs_clk_rate period in ps
	 */
	unsigned int		hs_skip;

	/**
	 * @hs_trail:
	 *
	 * Time, in picoseconds, that the transmitter drives the
	 * flipped differential state after last payload data bit of a
	 * HS transmission burst
	 *
	 * Minimum value: max(8 * @hs_clk_rate period in ps,
	 *		      60000 ps + 4 * @hs_clk_rate period in ps)
	 */
	unsigned int		hs_trail;

	/**
	 * @hs_zero:
	 *
	 * Time, in picoseconds, that the transmitter drives the HS-0
	 * state prior to transmitting the Sync sequence.
	 */
	unsigned int		hs_zero;

	/**
	 * @init:
	 *
	 * Time, in microseconds for the initialization period to
	 * complete.
	 *
	 * Minimum value: 100 us
	 */
	unsigned int		init;

	/**
	 * @lpx:
	 *
	 * Transmitted length, in picoseconds, of any Low-Power state
	 * period.
	 *
	 * Minimum value: 50000 ps
	 */
	unsigned int		lpx;

	/**
	 * @ta_get:
	 *
	 * Time, in picoseconds, that the new transmitter drives the
	 * Bridge state (LP-00) after accepting control during a Link
	 * Turnaround.
	 *
	 * Value: 5 * @lpx
	 */
	unsigned int		ta_get;

	/**
	 * @ta_go:
	 *
	 * Time, in picoseconds, that the transmitter drives the
	 * Bridge state (LP-00) before releasing control during a Link
	 * Turnaround.
	 *
	 * Value: 4 * @lpx
	 */
	unsigned int		ta_go;

	/**
	 * @ta_sure:
	 *
	 * Time, in picoseconds, that the new transmitter waits after
	 * the LP-10 state before transmitting the Bridge state
	 * (LP-00) during a Link Turnaround.
	 *
	 * Minimum value: @lpx
	 * Maximum value: 2 * @lpx
	 */
	unsigned int		ta_sure;

	/**
	 * @wakeup:
	 *
	 * Time, in microseconds, that a transmitter drives a Mark-1
	 * state prior to a Stop state in order to initiate an exit
	 * from ULPS.
	 *
	 * Minimum value: 1000 us
	 */
	unsigned int		wakeup;

	/**
	 * @hs_clk_rate:
	 *
	 * Clock rate, in Hertz, of the high-speed clock.
	 */
	unsigned long		hs_clk_rate;

	/**
	 * @lp_clk_rate:
	 *
	 * Clock rate, in Hertz, of the low-power clock.
	 */
	unsigned long		lp_clk_rate;

	/**
	 * @lanes:
	 *
	 * Number of active, consecutive, data lanes, starting from
	 * lane 0, used for the transmissions.
	 */
	unsigned char		lanes;
};

int phy_mipi_dphy_get_default_config(unsigned long pixel_clock,
				     unsigned int bpp,
				     unsigned int lanes,
				     struct phy_configure_opts_mipi_dphy *cfg);
int phy_mipi_dphy_get_default_config_for_hsclk(unsigned long long hs_clk_rate,
					       unsigned int lanes,
					       struct phy_configure_opts_mipi_dphy *cfg);
int phy_mipi_dphy_config_validate(struct phy_configure_opts_mipi_dphy *cfg);

#endif /* __PHY_MIPI_DPHY_H_ */
