/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * include/media/si476x-platform.h -- Platform data specific definitions
 *
 * Copyright (C) 2013 Andrey Smirnov
 *
 * Author: Andrey Smirnov <andrew.smirnov@gmail.com>
 */

#ifndef __SI476X_PLATFORM_H__
#define __SI476X_PLATFORM_H__

/* It is possible to select one of the four adresses using pins A0
 * and A1 on SI476x */
#define SI476X_I2C_ADDR_1	0x60
#define SI476X_I2C_ADDR_2	0x61
#define SI476X_I2C_ADDR_3	0x62
#define SI476X_I2C_ADDR_4	0x63

enum si476x_iqclk_config {
	SI476X_IQCLK_NOOP = 0,
	SI476X_IQCLK_TRISTATE = 1,
	SI476X_IQCLK_IQ = 21,
};
enum si476x_iqfs_config {
	SI476X_IQFS_NOOP = 0,
	SI476X_IQFS_TRISTATE = 1,
	SI476X_IQFS_IQ = 21,
};
enum si476x_iout_config {
	SI476X_IOUT_NOOP = 0,
	SI476X_IOUT_TRISTATE = 1,
	SI476X_IOUT_OUTPUT = 22,
};
enum si476x_qout_config {
	SI476X_QOUT_NOOP = 0,
	SI476X_QOUT_TRISTATE = 1,
	SI476X_QOUT_OUTPUT = 22,
};

enum si476x_dclk_config {
	SI476X_DCLK_NOOP      = 0,
	SI476X_DCLK_TRISTATE  = 1,
	SI476X_DCLK_DAUDIO    = 10,
};

enum si476x_dfs_config {
	SI476X_DFS_NOOP      = 0,
	SI476X_DFS_TRISTATE  = 1,
	SI476X_DFS_DAUDIO    = 10,
};

enum si476x_dout_config {
	SI476X_DOUT_NOOP       = 0,
	SI476X_DOUT_TRISTATE   = 1,
	SI476X_DOUT_I2S_OUTPUT = 12,
	SI476X_DOUT_I2S_INPUT  = 13,
};

enum si476x_xout_config {
	SI476X_XOUT_NOOP        = 0,
	SI476X_XOUT_TRISTATE    = 1,
	SI476X_XOUT_I2S_INPUT   = 13,
	SI476X_XOUT_MODE_SELECT = 23,
};

enum si476x_icin_config {
	SI476X_ICIN_NOOP	= 0,
	SI476X_ICIN_TRISTATE	= 1,
	SI476X_ICIN_GPO1_HIGH	= 2,
	SI476X_ICIN_GPO1_LOW	= 3,
	SI476X_ICIN_IC_LINK	= 30,
};

enum si476x_icip_config {
	SI476X_ICIP_NOOP	= 0,
	SI476X_ICIP_TRISTATE	= 1,
	SI476X_ICIP_GPO2_HIGH	= 2,
	SI476X_ICIP_GPO2_LOW	= 3,
	SI476X_ICIP_IC_LINK	= 30,
};

enum si476x_icon_config {
	SI476X_ICON_NOOP	= 0,
	SI476X_ICON_TRISTATE	= 1,
	SI476X_ICON_I2S		= 10,
	SI476X_ICON_IC_LINK	= 30,
};

enum si476x_icop_config {
	SI476X_ICOP_NOOP	= 0,
	SI476X_ICOP_TRISTATE	= 1,
	SI476X_ICOP_I2S		= 10,
	SI476X_ICOP_IC_LINK	= 30,
};


enum si476x_lrout_config {
	SI476X_LROUT_NOOP	= 0,
	SI476X_LROUT_TRISTATE	= 1,
	SI476X_LROUT_AUDIO	= 2,
	SI476X_LROUT_MPX	= 3,
};


enum si476x_intb_config {
	SI476X_INTB_NOOP     = 0,
	SI476X_INTB_TRISTATE = 1,
	SI476X_INTB_DAUDIO   = 10,
	SI476X_INTB_IRQ      = 40,
};

enum si476x_a1_config {
	SI476X_A1_NOOP     = 0,
	SI476X_A1_TRISTATE = 1,
	SI476X_A1_IRQ      = 40,
};


struct si476x_pinmux {
	enum si476x_dclk_config  dclk;
	enum si476x_dfs_config   dfs;
	enum si476x_dout_config  dout;
	enum si476x_xout_config  xout;

	enum si476x_iqclk_config iqclk;
	enum si476x_iqfs_config  iqfs;
	enum si476x_iout_config  iout;
	enum si476x_qout_config  qout;

	enum si476x_icin_config  icin;
	enum si476x_icip_config  icip;
	enum si476x_icon_config  icon;
	enum si476x_icop_config  icop;

	enum si476x_lrout_config lrout;

	enum si476x_intb_config  intb;
	enum si476x_a1_config    a1;
};

enum si476x_ibias6x {
	SI476X_IBIAS6X_OTHER			= 0,
	SI476X_IBIAS6X_RCVR1_NON_4MHZ_CLK	= 1,
};

enum si476x_xstart {
	SI476X_XSTART_MULTIPLE_TUNER	= 0x11,
	SI476X_XSTART_NORMAL		= 0x77,
};

enum si476x_freq {
	SI476X_FREQ_4_MHZ		= 0,
	SI476X_FREQ_37P209375_MHZ	= 1,
	SI476X_FREQ_36P4_MHZ		= 2,
	SI476X_FREQ_37P8_MHZ		=  3,
};

enum si476x_xmode {
	SI476X_XMODE_CRYSTAL_RCVR1	= 1,
	SI476X_XMODE_EXT_CLOCK		= 2,
	SI476X_XMODE_CRYSTAL_RCVR2_3	= 3,
};

enum si476x_xbiashc {
	SI476X_XBIASHC_SINGLE_RECEIVER = 0,
	SI476X_XBIASHC_MULTIPLE_RECEIVER = 1,
};

enum si476x_xbias {
	SI476X_XBIAS_RCVR2_3	= 0,
	SI476X_XBIAS_4MHZ_RCVR1 = 3,
	SI476X_XBIAS_RCVR1	= 7,
};

enum si476x_func {
	SI476X_FUNC_BOOTLOADER	= 0,
	SI476X_FUNC_FM_RECEIVER = 1,
	SI476X_FUNC_AM_RECEIVER = 2,
	SI476X_FUNC_WB_RECEIVER = 3,
};


/**
 * @xcload: Selects the amount of additional on-chip capacitance to
 *          be connected between XTAL1 and gnd and between XTAL2 and
 *          GND. One half of the capacitance value shown here is the
 *          additional load capacitance presented to the xtal. The
 *          minimum step size is 0.277 pF. Recommended value is 0x28
 *          but it will be layout dependent. Range is 0–0x3F i.e.
 *          (0–16.33 pF)
 * @ctsien: enable CTSINT(interrupt request when CTS condition
 *          arises) when set
 * @intsel: when set A1 pin becomes the interrupt pin; otherwise,
 *          INTB is the interrupt pin
 * @func:   selects the boot function of the device. I.e.
 *          SI476X_BOOTLOADER  - Boot loader
 *          SI476X_FM_RECEIVER - FM receiver
 *          SI476X_AM_RECEIVER - AM receiver
 *          SI476X_WB_RECEIVER - Weatherband receiver
 * @freq:   oscillator's crystal frequency:
 *          SI476X_XTAL_37P209375_MHZ - 37.209375 Mhz
 *          SI476X_XTAL_36P4_MHZ      - 36.4 Mhz
 *          SI476X_XTAL_37P8_MHZ      - 37.8 Mhz
 */
struct si476x_power_up_args {
	enum si476x_ibias6x ibias6x;
	enum si476x_xstart  xstart;
	u8   xcload;
	bool fastboot;
	enum si476x_xbiashc xbiashc;
	enum si476x_xbias   xbias;
	enum si476x_func    func;
	enum si476x_freq    freq;
	enum si476x_xmode   xmode;
};


/**
 * enum si476x_phase_diversity_mode - possbile phase diversity modes
 * for SI4764/5/6/7 chips.
 *
 * @SI476X_PHDIV_DISABLED:		Phase diversity feature is
 *					disabled.
 * @SI476X_PHDIV_PRIMARY_COMBINING:	Tuner works as a primary tuner
 *					in combination with a
 *					secondary one.
 * @SI476X_PHDIV_PRIMARY_ANTENNA:	Tuner works as a primary tuner
 *					using only its own antenna.
 * @SI476X_PHDIV_SECONDARY_ANTENNA:	Tuner works as a primary tuner
 *					usning seconary tuner's antenna.
 * @SI476X_PHDIV_SECONDARY_COMBINING:	Tuner works as a secondary
 *					tuner in combination with the
 *					primary one.
 */
enum si476x_phase_diversity_mode {
	SI476X_PHDIV_DISABLED			= 0,
	SI476X_PHDIV_PRIMARY_COMBINING		= 1,
	SI476X_PHDIV_PRIMARY_ANTENNA		= 2,
	SI476X_PHDIV_SECONDARY_ANTENNA		= 3,
	SI476X_PHDIV_SECONDARY_COMBINING	= 5,
};


/*
 * Platform dependent definition
 */
struct si476x_platform_data {
	int gpio_reset; /* < 0 if not used */

	struct si476x_power_up_args power_up_parameters;
	enum si476x_phase_diversity_mode diversity_mode;

	struct si476x_pinmux pinmux;
};


#endif /* __SI476X_PLATFORM_H__ */
