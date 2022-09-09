/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * include/media/si476x-core.h -- Common definitions for si476x core
 * device
 *
 * Copyright (C) 2012 Innovative Converged Devices(ICD)
 * Copyright (C) 2013 Andrey Smirnov
 *
 * Author: Andrey Smirnov <andrew.smirnov@gmail.com>
 */

#ifndef SI476X_CORE_H
#define SI476X_CORE_H

#include <linux/kfifo.h>
#include <linux/atomic.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/mutex.h>
#include <linux/mfd/core.h>
#include <linux/videodev2.h>
#include <linux/regulator/consumer.h>

#include <linux/mfd/si476x-platform.h>
#include <linux/mfd/si476x-reports.h>

/* Command Timeouts */
#define SI476X_DEFAULT_TIMEOUT	100000
#define SI476X_TIMEOUT_TUNE	700000
#define SI476X_TIMEOUT_POWER_UP	330000
#define SI476X_STATUS_POLL_US	0

/* -------------------- si476x-i2c.c ----------------------- */

enum si476x_freq_supported_chips {
	SI476X_CHIP_SI4761 = 1,
	SI476X_CHIP_SI4764,
	SI476X_CHIP_SI4768,
};

enum si476x_part_revisions {
	SI476X_REVISION_A10 = 0,
	SI476X_REVISION_A20 = 1,
	SI476X_REVISION_A30 = 2,
};

enum si476x_mfd_cells {
	SI476X_RADIO_CELL = 0,
	SI476X_CODEC_CELL,
	SI476X_MFD_CELLS,
};

/**
 * enum si476x_power_state - possible power state of the si476x
 * device.
 *
 * @SI476X_POWER_DOWN: In this state all regulators are turned off
 * and the reset line is pulled low. The device is completely
 * inactive.
 * @SI476X_POWER_UP_FULL: In this state all the power regulators are
 * turned on, reset line pulled high, IRQ line is enabled(polling is
 * active for polling use scenario) and device is turned on with
 * POWER_UP command. The device is ready to be used.
 * @SI476X_POWER_INCONSISTENT: This state indicates that previous
 * power down was inconsistent, meaning some of the regulators were
 * not turned down and thus use of the device, without power-cycling
 * is impossible.
 */
enum si476x_power_state {
	SI476X_POWER_DOWN		= 0,
	SI476X_POWER_UP_FULL		= 1,
	SI476X_POWER_INCONSISTENT	= 2,
};

/**
 * struct si476x_core - internal data structure representing the
 * underlying "core" device which all the MFD cell-devices use.
 *
 * @client: Actual I2C client used to transfer commands to the chip.
 * @chip_id: Last digit of the chip model(E.g. "1" for SI4761)
 * @cells: MFD cell devices created by this driver.
 * @cmd_lock: Mutex used to serialize all the requests to the core
 * device. This filed should not be used directly. Instead
 * si476x_core_lock()/si476x_core_unlock() should be used to get
 * exclusive access to the "core" device.
 * @users: Active users counter(Used by the radio cell)
 * @rds_read_queue: Wait queue used to wait for RDS data.
 * @rds_fifo: FIFO in which all the RDS data received from the chip is
 * placed.
 * @rds_fifo_drainer: Worker that drains on-chip RDS FIFO.
 * @rds_drainer_is_working: Flag used for launching only one instance
 * of the @rds_fifo_drainer.
 * @rds_drainer_status_lock: Lock used to guard access to the
 * @rds_drainer_is_working variable.
 * @command: Wait queue for wainting on the command comapletion.
 * @cts: Clear To Send flag set upon receiving first status with CTS
 * set.
 * @tuning: Wait queue used for wainting for tune/seek comand
 * completion.
 * @stc: Similar to @cts, but for the STC bit of the status value.
 * @power_up_parameters: Parameters used as argument for POWER_UP
 * command when the device is started.
 * @state: Current power state of the device.
 * @supplues: Structure containing handles to all power supplies used
 * by the device (NULL ones are ignored).
 * @gpio_reset: GPIO pin connectet to the RSTB pin of the chip.
 * @pinmux: Chip's configurable pins configuration.
 * @diversity_mode: Chips role when functioning in diversity mode.
 * @status_monitor: Polling worker used in polling use case scenarion
 * (when IRQ is not avalible).
 * @revision: Chip's running firmware revision number(Used for correct
 * command set support).
 */

struct si476x_core {
	struct i2c_client *client;
	struct regmap *regmap;
	int chip_id;
	struct mfd_cell cells[SI476X_MFD_CELLS];

	struct mutex cmd_lock; /* for serializing fm radio operations */
	atomic_t users;

	wait_queue_head_t  rds_read_queue;
	struct kfifo       rds_fifo;
	struct work_struct rds_fifo_drainer;
	bool               rds_drainer_is_working;
	struct mutex       rds_drainer_status_lock;

	wait_queue_head_t command;
	atomic_t          cts;

	wait_queue_head_t tuning;
	atomic_t          stc;

	struct si476x_power_up_args power_up_parameters;

	enum si476x_power_state power_state;

	struct regulator_bulk_data supplies[4];

	int gpio_reset;

	struct si476x_pinmux pinmux;
	enum si476x_phase_diversity_mode diversity_mode;

	atomic_t is_alive;

	struct delayed_work status_monitor;
#define SI476X_WORK_TO_CORE(w) container_of(to_delayed_work(w),	\
					    struct si476x_core,	\
					    status_monitor)

	int revision;

	int rds_fifo_depth;
};

static inline struct si476x_core *i2c_mfd_cell_to_core(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev->parent);
	return i2c_get_clientdata(client);
}


/**
 * si476x_core_lock() - lock the core device to get an exclusive access
 * to it.
 */
static inline void si476x_core_lock(struct si476x_core *core)
{
	mutex_lock(&core->cmd_lock);
}

/**
 * si476x_core_unlock() - unlock the core device to relinquish an
 * exclusive access to it.
 */
static inline void si476x_core_unlock(struct si476x_core *core)
{
	mutex_unlock(&core->cmd_lock);
}

/* *_TUNE_FREQ family of commands accept frequency in multiples of
    10kHz */
static inline u16 hz_to_si476x(struct si476x_core *core, int freq)
{
	u16 result;

	switch (core->power_up_parameters.func) {
	default:
	case SI476X_FUNC_FM_RECEIVER:
		result = freq / 10000;
		break;
	case SI476X_FUNC_AM_RECEIVER:
		result = freq / 1000;
		break;
	}

	return result;
}

static inline int si476x_to_hz(struct si476x_core *core, u16 freq)
{
	int result;

	switch (core->power_up_parameters.func) {
	default:
	case SI476X_FUNC_FM_RECEIVER:
		result = freq * 10000;
		break;
	case SI476X_FUNC_AM_RECEIVER:
		result = freq * 1000;
		break;
	}

	return result;
}

/* Since the V4L2_TUNER_CAP_LOW flag is supplied, V4L2 subsystem
 * mesures frequency in 62.5 Hz units */

static inline int hz_to_v4l2(int freq)
{
	return (freq * 10) / 625;
}

static inline int v4l2_to_hz(int freq)
{
	return (freq * 625) / 10;
}

static inline u16 v4l2_to_si476x(struct si476x_core *core, int freq)
{
	return hz_to_si476x(core, v4l2_to_hz(freq));
}

static inline int si476x_to_v4l2(struct si476x_core *core, u16 freq)
{
	return hz_to_v4l2(si476x_to_hz(core, freq));
}



/**
 * struct si476x_func_info - structure containing result of the
 * FUNC_INFO command.
 *
 * @firmware.major: Firmware major number.
 * @firmware.minor[...]: Firmware minor numbers.
 * @patch_id:
 * @func: Mode tuner is working in.
 */
struct si476x_func_info {
	struct {
		u8 major, minor[2];
	} firmware;
	u16 patch_id;
	enum si476x_func func;
};

/**
 * struct si476x_power_down_args - structure used to pass parameters
 * to POWER_DOWN command
 *
 * @xosc: true - Power down, but leav oscillator running.
 *        false - Full power down.
 */
struct si476x_power_down_args {
	bool xosc;
};

/**
 * enum si476x_tunemode - enum representing possible tune modes for
 * the chip.
 * @SI476X_TM_VALIDATED_NORMAL_TUNE: Unconditionally stay on the new
 * channel after tune, tune status is valid.
 * @SI476X_TM_INVALIDATED_FAST_TUNE: Unconditionally stay in the new
 * channel after tune, tune status invalid.
 * @SI476X_TM_VALIDATED_AF_TUNE: Jump back to previous channel if
 * metric thresholds are not met.
 * @SI476X_TM_VALIDATED_AF_CHECK: Unconditionally jump back to the
 * previous channel.
 */
enum si476x_tunemode {
	SI476X_TM_VALIDATED_NORMAL_TUNE = 0,
	SI476X_TM_INVALIDATED_FAST_TUNE = 1,
	SI476X_TM_VALIDATED_AF_TUNE     = 2,
	SI476X_TM_VALIDATED_AF_CHECK    = 3,
};

/**
 * enum si476x_smoothmetrics - enum containing the possible setting fo
 * audio transitioning of the chip
 * @SI476X_SM_INITIALIZE_AUDIO: Initialize audio state to match this
 * new channel
 * @SI476X_SM_TRANSITION_AUDIO: Transition audio state from previous
 * channel values to the new values
 */
enum si476x_smoothmetrics {
	SI476X_SM_INITIALIZE_AUDIO = 0,
	SI476X_SM_TRANSITION_AUDIO = 1,
};

/**
 * struct si476x_rds_status_report - the structure representing the
 * response to 'FM_RD_STATUS' command
 * @rdstpptyint: Traffic program flag(TP) and/or program type(PTY)
 * code has changed.
 * @rdspiint: Program identification(PI) code has changed.
 * @rdssyncint: RDS synchronization has changed.
 * @rdsfifoint: RDS was received and the RDS FIFO has at least
 * 'FM_RDS_INTERRUPT_FIFO_COUNT' elements in it.
 * @tpptyvalid: TP flag and PTY code are valid falg.
 * @pivalid: PI code is valid flag.
 * @rdssync: RDS is currently synchronized.
 * @rdsfifolost: On or more RDS groups have been lost/discarded flag.
 * @tp: Current channel's TP flag.
 * @pty: Current channel's PTY code.
 * @pi: Current channel's PI code.
 * @rdsfifoused: Number of blocks remaining in the RDS FIFO (0 if
 * empty).
 */
struct si476x_rds_status_report {
	bool rdstpptyint, rdspiint, rdssyncint, rdsfifoint;
	bool tpptyvalid, pivalid, rdssync, rdsfifolost;
	bool tp;

	u8 pty;
	u16 pi;

	u8 rdsfifoused;
	u8 ble[4];

	struct v4l2_rds_data rds[4];
};

struct si476x_rsq_status_args {
	bool primary;
	bool rsqack;
	bool attune;
	bool cancel;
	bool stcack;
};

enum si476x_injside {
	SI476X_INJSIDE_AUTO	= 0,
	SI476X_INJSIDE_LOW	= 1,
	SI476X_INJSIDE_HIGH	= 2,
};

struct si476x_tune_freq_args {
	bool zifsr;
	bool hd;
	enum si476x_injside injside;
	int freq;
	enum si476x_tunemode tunemode;
	enum si476x_smoothmetrics smoothmetrics;
	int antcap;
};

int  si476x_core_stop(struct si476x_core *, bool);
int  si476x_core_start(struct si476x_core *, bool);
int  si476x_core_set_power_state(struct si476x_core *, enum si476x_power_state);
bool si476x_core_has_am(struct si476x_core *);
bool si476x_core_has_diversity(struct si476x_core *);
bool si476x_core_is_a_secondary_tuner(struct si476x_core *);
bool si476x_core_is_a_primary_tuner(struct si476x_core *);
bool si476x_core_is_in_am_receiver_mode(struct si476x_core *core);
bool si476x_core_is_powered_up(struct si476x_core *core);

enum si476x_i2c_type {
	SI476X_I2C_SEND,
	SI476X_I2C_RECV
};

int si476x_core_i2c_xfer(struct si476x_core *,
			 enum si476x_i2c_type,
			 char *, int);


/* -------------------- si476x-cmd.c ----------------------- */

int si476x_core_cmd_func_info(struct si476x_core *, struct si476x_func_info *);
int si476x_core_cmd_set_property(struct si476x_core *, u16, u16);
int si476x_core_cmd_get_property(struct si476x_core *, u16);
int si476x_core_cmd_dig_audio_pin_cfg(struct si476x_core *,
				      enum si476x_dclk_config,
				      enum si476x_dfs_config,
				      enum si476x_dout_config,
				      enum si476x_xout_config);
int si476x_core_cmd_zif_pin_cfg(struct si476x_core *,
				enum si476x_iqclk_config,
				enum si476x_iqfs_config,
				enum si476x_iout_config,
				enum si476x_qout_config);
int si476x_core_cmd_ic_link_gpo_ctl_pin_cfg(struct si476x_core *,
					    enum si476x_icin_config,
					    enum si476x_icip_config,
					    enum si476x_icon_config,
					    enum si476x_icop_config);
int si476x_core_cmd_ana_audio_pin_cfg(struct si476x_core *,
				      enum si476x_lrout_config);
int si476x_core_cmd_intb_pin_cfg(struct si476x_core *, enum si476x_intb_config,
				 enum si476x_a1_config);
int si476x_core_cmd_fm_seek_start(struct si476x_core *, bool, bool);
int si476x_core_cmd_am_seek_start(struct si476x_core *, bool, bool);
int si476x_core_cmd_fm_rds_status(struct si476x_core *, bool, bool, bool,
				  struct si476x_rds_status_report *);
int si476x_core_cmd_fm_rds_blockcount(struct si476x_core *, bool,
				      struct si476x_rds_blockcount_report *);
int si476x_core_cmd_fm_tune_freq(struct si476x_core *,
				 struct si476x_tune_freq_args *);
int si476x_core_cmd_am_tune_freq(struct si476x_core *,
				 struct si476x_tune_freq_args *);
int si476x_core_cmd_am_rsq_status(struct si476x_core *,
				  struct si476x_rsq_status_args *,
				  struct si476x_rsq_status_report *);
int si476x_core_cmd_fm_rsq_status(struct si476x_core *,
				  struct si476x_rsq_status_args *,
				  struct si476x_rsq_status_report *);
int si476x_core_cmd_power_up(struct si476x_core *,
			     struct si476x_power_up_args *);
int si476x_core_cmd_power_down(struct si476x_core *,
			       struct si476x_power_down_args *);
int si476x_core_cmd_fm_phase_div_status(struct si476x_core *);
int si476x_core_cmd_fm_phase_diversity(struct si476x_core *,
				       enum si476x_phase_diversity_mode);

int si476x_core_cmd_fm_acf_status(struct si476x_core *,
				  struct si476x_acf_status_report *);
int si476x_core_cmd_am_acf_status(struct si476x_core *,
				  struct si476x_acf_status_report *);
int si476x_core_cmd_agc_status(struct si476x_core *,
			       struct si476x_agc_status_report *);

enum si476x_power_grid_type {
	SI476X_POWER_GRID_50HZ = 0,
	SI476X_POWER_GRID_60HZ,
};

/* Properties  */

enum si476x_interrupt_flags {
	SI476X_STCIEN = (1 << 0),
	SI476X_ACFIEN = (1 << 1),
	SI476X_RDSIEN = (1 << 2),
	SI476X_RSQIEN = (1 << 3),

	SI476X_ERRIEN = (1 << 6),
	SI476X_CTSIEN = (1 << 7),

	SI476X_STCREP = (1 << 8),
	SI476X_ACFREP = (1 << 9),
	SI476X_RDSREP = (1 << 10),
	SI476X_RSQREP = (1 << 11),
};

enum si476x_rdsint_sources {
	SI476X_RDSTPPTY = (1 << 4),
	SI476X_RDSPI    = (1 << 3),
	SI476X_RDSSYNC	= (1 << 1),
	SI476X_RDSRECV	= (1 << 0),
};

enum si476x_status_response_bits {
	SI476X_CTS	  = (1 << 7),
	SI476X_ERR	  = (1 << 6),
	/* Status response for WB receiver */
	SI476X_WB_ASQ_INT = (1 << 4),
	SI476X_RSQ_INT    = (1 << 3),
	/* Status response for FM receiver */
	SI476X_FM_RDS_INT = (1 << 2),
	SI476X_ACF_INT    = (1 << 1),
	SI476X_STC_INT    = (1 << 0),
};

/* -------------------- si476x-prop.c ----------------------- */

enum si476x_common_receiver_properties {
	SI476X_PROP_INT_CTL_ENABLE			= 0x0000,
	SI476X_PROP_DIGITAL_IO_INPUT_SAMPLE_RATE	= 0x0200,
	SI476X_PROP_DIGITAL_IO_INPUT_FORMAT		= 0x0201,
	SI476X_PROP_DIGITAL_IO_OUTPUT_SAMPLE_RATE	= 0x0202,
	SI476X_PROP_DIGITAL_IO_OUTPUT_FORMAT		= 0x0203,

	SI476X_PROP_SEEK_BAND_BOTTOM			= 0x1100,
	SI476X_PROP_SEEK_BAND_TOP			= 0x1101,
	SI476X_PROP_SEEK_FREQUENCY_SPACING		= 0x1102,

	SI476X_PROP_VALID_MAX_TUNE_ERROR		= 0x2000,
	SI476X_PROP_VALID_SNR_THRESHOLD			= 0x2003,
	SI476X_PROP_VALID_RSSI_THRESHOLD		= 0x2004,
};

enum si476x_am_receiver_properties {
	SI476X_PROP_AUDIO_PWR_LINE_FILTER		= 0x0303,
};

enum si476x_fm_receiver_properties {
	SI476X_PROP_AUDIO_DEEMPHASIS			= 0x0302,

	SI476X_PROP_FM_RDS_INTERRUPT_SOURCE		= 0x4000,
	SI476X_PROP_FM_RDS_INTERRUPT_FIFO_COUNT		= 0x4001,
	SI476X_PROP_FM_RDS_CONFIG			= 0x4002,
};

enum si476x_prop_audio_pwr_line_filter_bits {
	SI476X_PROP_PWR_HARMONICS_MASK	= 0x001f,
	SI476X_PROP_PWR_GRID_MASK	= 0x0100,
	SI476X_PROP_PWR_ENABLE_MASK	= 0x0200,
	SI476X_PROP_PWR_GRID_50HZ	= 0x0000,
	SI476X_PROP_PWR_GRID_60HZ	= 0x0100,
};

enum si476x_prop_fm_rds_config_bits {
	SI476X_PROP_RDSEN_MASK	= 0x1,
	SI476X_PROP_RDSEN	= 0x1,
};


struct regmap *devm_regmap_init_si476x(struct si476x_core *);

#endif	/* SI476X_CORE_H */
