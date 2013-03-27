/*
 * include/media/si476x.h -- Common definitions for si476x driver
 *
 * Copyright (C) 2012 Innovative Converged Devices(ICD)
 * Copyright (C) 2013 Andrey Smirnov
 *
 * Author: Andrey Smirnov <andrew.smirnov@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */

#ifndef SI476X_H
#define SI476X_H

#include <linux/types.h>
#include <linux/videodev2.h>

struct si476x_device;

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

enum si476x_part_revisions {
	SI476X_REVISION_A10 = 0,
	SI476X_REVISION_A20 = 1,
	SI476X_REVISION_A30 = 2,
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


enum si476x_ctrl_id {
	V4L2_CID_SI476X_RSSI_THRESHOLD	= (V4L2_CID_USER_SI476X_BASE + 1),
	V4L2_CID_SI476X_SNR_THRESHOLD	= (V4L2_CID_USER_SI476X_BASE + 2),
	V4L2_CID_SI476X_MAX_TUNE_ERROR	= (V4L2_CID_USER_SI476X_BASE + 3),
	V4L2_CID_SI476X_HARMONICS_COUNT	= (V4L2_CID_USER_SI476X_BASE + 4),
	V4L2_CID_SI476X_DIVERSITY_MODE	= (V4L2_CID_USER_SI476X_BASE + 5),
	V4L2_CID_SI476X_INTERCHIP_LINK	= (V4L2_CID_USER_SI476X_BASE + 6),
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

/**
 * struct si476x_rsq_status - structure containing received signal
 * quality
 * @multhint:   Multipath Detect High.
 *              true  - Indicatedes that the value is below
 *                      FM_RSQ_MULTIPATH_HIGH_THRESHOLD
 *              false - Indicatedes that the value is above
 *                      FM_RSQ_MULTIPATH_HIGH_THRESHOLD
 * @multlint:   Multipath Detect Low.
 *              true  - Indicatedes that the value is below
 *                      FM_RSQ_MULTIPATH_LOW_THRESHOLD
 *              false - Indicatedes that the value is above
 *                      FM_RSQ_MULTIPATH_LOW_THRESHOLD
 * @snrhint:    SNR Detect High.
 *              true  - Indicatedes that the value is below
 *                      FM_RSQ_SNR_HIGH_THRESHOLD
 *              false - Indicatedes that the value is above
 *                      FM_RSQ_SNR_HIGH_THRESHOLD
 * @snrlint:    SNR Detect Low.
 *              true  - Indicatedes that the value is below
 *                      FM_RSQ_SNR_LOW_THRESHOLD
 *              false - Indicatedes that the value is above
 *                      FM_RSQ_SNR_LOW_THRESHOLD
 * @rssihint:   RSSI Detect High.
 *              true  - Indicatedes that the value is below
 *                      FM_RSQ_RSSI_HIGH_THRESHOLD
 *              false - Indicatedes that the value is above
 *                      FM_RSQ_RSSI_HIGH_THRESHOLD
 * @rssilint:   RSSI Detect Low.
 *              true  - Indicatedes that the value is below
 *                      FM_RSQ_RSSI_LOW_THRESHOLD
 *              false - Indicatedes that the value is above
 *                      FM_RSQ_RSSI_LOW_THRESHOLD
 * @bltf:       Band Limit.
 *              Set if seek command hits the band limit or wrapped to
 *              the original frequency.
 * @snr_ready:  SNR measurement in progress.
 * @rssiready:  RSSI measurement in progress.
 * @afcrl:      Set if FREQOFF >= MAX_TUNE_ERROR
 * @valid:      Set if the channel is valid
 *               rssi < FM_VALID_RSSI_THRESHOLD
 *               snr  < FM_VALID_SNR_THRESHOLD
 *               tune_error < FM_VALID_MAX_TUNE_ERROR
 * @readfreq:   Current tuned frequency.
 * @freqoff:    Signed frequency offset.
 * @rssi:       Received Signal Strength Indicator(dBuV).
 * @snr:        RF SNR Indicator(dB).
 * @lassi:
 * @hassi:      Low/High side Adjacent(100 kHz) Channel Strength Indicator
 * @mult:       Multipath indicator
 * @dev:        Who knows? But values may vary.
 * @readantcap: Antenna tuning capacity value.
 * @assi:       Adjacent Channel(+/- 200kHz) Strength Indicator
 * @usn:        Ultrasonic Noise Inticator in -DBFS
 */
struct si476x_rsq_status_report {
	__u8 multhint, multlint;
	__u8 snrhint,  snrlint;
	__u8 rssihint, rssilint;
	__u8 bltf;
	__u8 snr_ready;
	__u8 rssiready;
	__u8 injside;
	__u8 afcrl;
	__u8 valid;

	__u16 readfreq;
	__s8  freqoff;
	__s8  rssi;
	__s8  snr;
	__s8  issi;
	__s8  lassi, hassi;
	__s8  mult;
	__u8  dev;
	__u16 readantcap;
	__s8  assi;
	__s8  usn;

	__u8 pilotdev;
	__u8 rdsdev;
	__u8 assidev;
	__u8 strongdev;
	__u16 rdspi;
} __packed;

/**
 * si476x_acf_status_report - ACF report results
 *
 * @blend_int: If set, indicates that stereo separation has crossed
 * below the blend threshold as set by FM_ACF_BLEND_THRESHOLD
 * @hblend_int: If set, indicates that HiBlend cutoff frequency is
 * lower than threshold as set by FM_ACF_HBLEND_THRESHOLD
 * @hicut_int:  If set, indicates that HiCut cutoff frequency is lower
 * than the threshold set by ACF_

 */
struct si476x_acf_status_report {
	__u8 blend_int;
	__u8 hblend_int;
	__u8 hicut_int;
	__u8 chbw_int;
	__u8 softmute_int;
	__u8 smute;
	__u8 smattn;
	__u8 chbw;
	__u8 hicut;
	__u8 hiblend;
	__u8 pilot;
	__u8 stblend;
} __packed;

enum si476x_fmagc {
	SI476X_FMAGC_10K_OHM	= 0,
	SI476X_FMAGC_800_OHM	= 1,
	SI476X_FMAGC_400_OHM	= 2,
	SI476X_FMAGC_200_OHM	= 4,
	SI476X_FMAGC_100_OHM	= 8,
	SI476X_FMAGC_50_OHM	= 16,
	SI476X_FMAGC_25_OHM	= 32,
	SI476X_FMAGC_12P5_OHM	= 64,
	SI476X_FMAGC_6P25_OHM	= 128,
};

struct si476x_agc_status_report {
	__u8 mxhi;
	__u8 mxlo;
	__u8 lnahi;
	__u8 lnalo;
	__u8 fmagc1;
	__u8 fmagc2;
	__u8 pgagain;
	__u8 fmwblang;
} __packed;

struct si476x_rds_blockcount_report {
	__u16 expected;
	__u16 received;
	__u16 uncorrectable;
} __packed;

#endif /* SI476X_H*/
