/*-
 * Copyright (c) 2015,2016 Annapurna Labs Ltd. and affiliates
 * All rights reserved.
 *
 * Developed by Semihalf.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include "al_init_eth_lm.h"
#include "al_serdes.h"
#include "al_hal_eth.h"
#include "al_init_eth_kr.h"

/**
 *  @{
 * @file   al_init_eth_lm.c
 *
 * @brief ethernet link management common utilities
 *
 */

/* delay before checking link status with new serdes parameters (uSec) */
#define	AL_ETH_LM_LINK_STATUS_DELAY	1000
/* delay before checking link status after reconfiguring the retimer (uSec) */
#define	AL_ETH_LM_RETIMER_LINK_STATUS_DELAY 50000

#define	AL_ETH_LM_EQ_ITERATIONS		15
#define	AL_ETH_LM_MAX_DCGAIN		8

/* num of link training failures till serdes reset */
#define	AL_ETH_LT_FAILURES_TO_RESET	10

#define	MODULE_IDENTIFIER_IDX		0
#define	MODULE_IDENTIFIER_SFP		0x3
#define	MODULE_IDENTIFIER_QSFP		0xd

#define	SFP_PRESENT			0
#define	SFP_NOT_PRESENT			1

/* SFP+ module */
#define	SFP_I2C_HEADER_10G_IDX		3
#define	SFP_I2C_HEADER_10G_DA_IDX	8
#define	SFP_I2C_HEADER_10G_DA_LEN_IDX	18
#define	SFP_I2C_HEADER_1G_IDX		6
#define	SFP_I2C_HEADER_SIGNAL_RATE	12 /* Nominal signaling rate, units of 100MBd. */

#define	SFP_MIN_SIGNAL_RATE_25G		250
#define	SFP_MIN_SIGNAL_RATE_10G		100

/* QSFP+ module */
#define	QSFP_COMPLIANCE_CODE_IDX	131
/* 40GBASE-LR4 and 40GBASE-SR4 are optic modules */
#define	QSFP_COMPLIANCE_CODE_OPTIC	((1 << 1) | (1 << 2))
#define	QSFP_COMPLIANCE_CODE_DAC	(1 << 3)
#define	QSFP_CABLE_LEN_IDX		146

/* TODO: need to check the necessary delay */
#define	AL_ETH_LM_RETIMER_WAIT_FOR_LOCK	500 /* delay after retimer reset to lock (mSec) */
#define	AL_ETH_LM_SERDES_WAIT_FOR_LOCK	50 /* delay after signal detect to lock (mSec) */

#define AL_ETH_LM_GEARBOX_RESET_DELAY	1000 /* (uSec) */

static const uint32_t
al_eth_retimer_boost_addr[AL_ETH_RETIMER_CHANNEL_MAX][AL_ETH_RETIMER_TYPE_MAX] = {
					/* BR_210  |  BR_410 */
	/* AL_ETH_RETIMER_CHANNEL_A */	{0xf,		0x1a},
	/* AL_ETH_RETIMER_CHANNEL_B */	{0x16,		0x18},
	/* AL_ETH_RETIMER_CHANNEL_C */	{0x0,		0x16},
	/* AL_ETH_RETIMER_CHANNEL_D */	{0x0,		0x14},
};

#define	RETIMER_LENS_MAX		5
static const uint32_t
al_eth_retimer_boost_lens[RETIMER_LENS_MAX] = {0, 1, 2, 3, 5};

static const uint32_t
al_eth_retimer_boost_value[RETIMER_LENS_MAX + 1][AL_ETH_RETIMER_TYPE_MAX] = {
		/* BR_210  |  BR_410 */
	/* 0 */	{0x0,		0x0},
	/* 1 */	{0x1,		0x1},
	/* 2 */	{0x2,		0x1},
	/* 3 */	{0x3,		0x3},
	/* 5 */	{0x7,		0x3},
	/* 5+ */{0xb,		0x7},
};

struct retimer_config_reg {
	uint8_t addr;
	uint8_t value;
	uint8_t mask;
};

static struct retimer_config_reg retimer_ds25_25g_mode_tx_ch[] = {
	{.addr = 0x0A, .value = 0x0C, .mask = 0xff },
	{.addr = 0x2F, .value = 0x54, .mask = 0xff },
	{.addr = 0x31, .value = 0x20, .mask = 0xff },
	{.addr = 0x1E, .value = 0xE9, .mask = 0xff },
	{.addr = 0x1F, .value = 0x0B, .mask = 0xff },
	{.addr = 0xA6, .value = 0x43, .mask = 0xff },
	{.addr = 0x2A, .value = 0x5A, .mask = 0xff },
	{.addr = 0x2B, .value = 0x0A, .mask = 0xff },
	{.addr = 0x2C, .value = 0xF6, .mask = 0xff },
	{.addr = 0x70, .value = 0x05, .mask = 0xff },
	{.addr = 0x6A, .value = 0x21, .mask = 0xff },
	{.addr = 0x35, .value = 0x0F, .mask = 0xff },
	{.addr = 0x12, .value = 0x83, .mask = 0xff },
	{.addr = 0x9C, .value = 0x24, .mask = 0xff },
	{.addr = 0x98, .value = 0x00, .mask = 0xff },
	{.addr = 0x42, .value = 0x50, .mask = 0xff },
	{.addr = 0x44, .value = 0x90, .mask = 0xff },
	{.addr = 0x45, .value = 0xC0, .mask = 0xff },
	{.addr = 0x46, .value = 0xD0, .mask = 0xff },
	{.addr = 0x47, .value = 0xD1, .mask = 0xff },
	{.addr = 0x48, .value = 0xD5, .mask = 0xff },
	{.addr = 0x49, .value = 0xD8, .mask = 0xff },
	{.addr = 0x4A, .value = 0xEA, .mask = 0xff },
	{.addr = 0x4B, .value = 0xF7, .mask = 0xff },
	{.addr = 0x4C, .value = 0xFD, .mask = 0xff },
	{.addr = 0x8E, .value = 0x00, .mask = 0xff },
	{.addr = 0x3D, .value = 0x94, .mask = 0xff },
	{.addr = 0x3F, .value = 0x40, .mask = 0xff },
	{.addr = 0x3E, .value = 0x43, .mask = 0xff },
	{.addr = 0x0A, .value = 0x00, .mask = 0xff },
};

static struct retimer_config_reg retimer_ds25_25g_mode_rx_ch[] = {
	{.addr = 0x0A, .value = 0x0C, .mask = 0xff},
	{.addr = 0x2F, .value = 0x54, .mask = 0xff},
	{.addr = 0x31, .value = 0x40, .mask = 0xff},
	{.addr = 0x1E, .value = 0xE3, .mask = 0xff},
	{.addr = 0x1F, .value = 0x0B, .mask = 0xff},
	{.addr = 0xA6, .value = 0x43, .mask = 0xff},
	{.addr = 0x2A, .value = 0x5A, .mask = 0xff},
	{.addr = 0x2B, .value = 0x0A, .mask = 0xff},
	{.addr = 0x2C, .value = 0xF6, .mask = 0xff},
	{.addr = 0x70, .value = 0x05, .mask = 0xff},
	{.addr = 0x6A, .value = 0x21, .mask = 0xff},
	{.addr = 0x35, .value = 0x0F, .mask = 0xff},
	{.addr = 0x12, .value = 0x83, .mask = 0xff},
	{.addr = 0x9C, .value = 0x24, .mask = 0xff},
	{.addr = 0x98, .value = 0x00, .mask = 0xff},
	{.addr = 0x42, .value = 0x50, .mask = 0xff},
	{.addr = 0x44, .value = 0x90, .mask = 0xff},
	{.addr = 0x45, .value = 0xC0, .mask = 0xff},
	{.addr = 0x46, .value = 0xD0, .mask = 0xff},
	{.addr = 0x47, .value = 0xD1, .mask = 0xff},
	{.addr = 0x48, .value = 0xD5, .mask = 0xff},
	{.addr = 0x49, .value = 0xD8, .mask = 0xff},
	{.addr = 0x4A, .value = 0xEA, .mask = 0xff},
	{.addr = 0x4B, .value = 0xF7, .mask = 0xff},
	{.addr = 0x4C, .value = 0xFD, .mask = 0xff},
	{.addr = 0x8E, .value = 0x00, .mask = 0xff},
	{.addr = 0x3D, .value = 0x94, .mask = 0xff},
	{.addr = 0x3F, .value = 0x40, .mask = 0xff},
	{.addr = 0x3E, .value = 0x43, .mask = 0xff},
	{.addr = 0x0A, .value = 0x00, .mask = 0xff},
};

static struct retimer_config_reg retimer_ds25_10g_mode[] = {
	/* Assert CDR reset (6.3) */
	{.addr = 0x0A, .value = 0x0C, .mask = 0x0C},
	/* Select 10.3125Gbps standard rate mode (6.6) */
	{.addr = 0x2F, .value = 0x00, .mask = 0xF0},
	/* Enable loop filter auto-adjust */
	{.addr = 0x1F, .value = 0x08, .mask = 0x08},
	/* Set Adapt Mode 1 (6.13) */
	{.addr = 0x31, .value = 0x20, .mask = 0x60},
	/* Disable the DFE since most applications do not need it (6.18) */
	{.addr = 0x1E, .value = 0x08, .mask = 0x08},
	/* Release CDR reset (6.4) */
	{.addr = 0x0A, .value = 0x00, .mask = 0x0C},
	/* Enable FIR (6.12) */
	{.addr = 0x3D, .value = 0x80, .mask = 0x80},
	/* Set Main-cursor tap sign to positive (6.12) */
	{.addr = 0x3D, .value = 0x00, .mask = 0x40},
	/* Set Post-cursor tap sign to negative (6.12) */
	{.addr = 0x3F, .value = 0x40, .mask = 0x40},
	/* Set Pre-cursor tap sign to negative (6.12) */
	{.addr = 0x3E, .value = 0x40, .mask = 0x40},
	/* Set Main-cursor tap magnitude to 13 (6.12) */
	{.addr = 0x3D, .value = 0x0D, .mask = 0x1F},
};

static int al_eth_lm_retimer_boost_config(struct al_eth_lm_context *lm_context);
static int al_eth_lm_retimer_ds25_full_config(struct al_eth_lm_context *lm_context);
static al_bool al_eth_lm_retimer_ds25_signal_detect(
		struct al_eth_lm_context *lm_context, uint32_t channel);
static int al_eth_lm_retimer_ds25_cdr_reset(struct al_eth_lm_context *lm_context, uint32_t channel);
static al_bool al_eth_lm_retimer_ds25_cdr_lock(
		struct al_eth_lm_context *lm_context, uint32_t channel);
static int al_eth_lm_retimer_25g_rx_adaptation(struct al_eth_lm_context *lm_context);

struct al_eth_lm_retimer {
	int (*config)(struct al_eth_lm_context *lm_context);
	int (*reset)(struct al_eth_lm_context *lm_context, uint32_t channel);
	int (*signal_detect)(struct al_eth_lm_context *lm_context, uint32_t channel);
	int (*cdr_lock)(struct al_eth_lm_context *lm_context, uint32_t channel);
	int (*rx_adaptation)(struct al_eth_lm_context *lm_context);
};

static struct al_eth_lm_retimer retimer[] = {
	{.config = al_eth_lm_retimer_boost_config, .signal_detect = NULL,
		.reset = NULL, .cdr_lock = NULL, .rx_adaptation = NULL},
	{.config = al_eth_lm_retimer_boost_config, .signal_detect = NULL,
		.reset = NULL, .cdr_lock = NULL, .rx_adaptation = NULL},
	{.config = al_eth_lm_retimer_ds25_full_config,
		.signal_detect = al_eth_lm_retimer_ds25_signal_detect,
		.reset = al_eth_lm_retimer_ds25_cdr_reset,
		.cdr_lock = al_eth_lm_retimer_ds25_cdr_lock,
		.rx_adaptation = al_eth_lm_retimer_25g_rx_adaptation},
};

#define SFP_10G_DA_ACTIVE		0x8
#define SFP_10G_DA_PASSIVE		0x4

#define lm_debug(...)				\
	do {					\
		if (lm_context->debug)		\
			al_warn(__VA_ARGS__);	\
		else				\
			al_dbg(__VA_ARGS__);	\
	} while (0)

static int
al_eth_sfp_detect(struct al_eth_lm_context *lm_context,
    enum al_eth_lm_link_mode *new_mode)
{
	int rc = 0;
	uint8_t sfp_10g;
	uint8_t sfp_1g;
	uint8_t sfp_cable_tech;
	uint8_t sfp_da_len;
	uint8_t signal_rate;

	do {
		rc = lm_context->i2c_read(lm_context->i2c_context,
		    lm_context->sfp_bus_id, lm_context->sfp_i2c_addr,
		    SFP_I2C_HEADER_10G_IDX, &sfp_10g);
		if (rc != 0)
			break;

		rc = lm_context->i2c_read(lm_context->i2c_context,
		    lm_context->sfp_bus_id, lm_context->sfp_i2c_addr,
		    SFP_I2C_HEADER_1G_IDX, &sfp_1g);
		if (rc != 0)
			break;

		rc = lm_context->i2c_read(lm_context->i2c_context,
		    lm_context->sfp_bus_id, lm_context->sfp_i2c_addr,
		    SFP_I2C_HEADER_10G_DA_IDX, &sfp_cable_tech);
		if (rc != 0)
			break;

		rc = lm_context->i2c_read(lm_context->i2c_context,
		    lm_context->sfp_bus_id, lm_context->sfp_i2c_addr,
		    SFP_I2C_HEADER_10G_DA_LEN_IDX, &sfp_da_len);
		if (rc != 0)
			break;

		rc = lm_context->i2c_read(lm_context->i2c_context,
					  lm_context->sfp_bus_id,
					  lm_context->sfp_i2c_addr,
					  SFP_I2C_HEADER_SIGNAL_RATE,
					  &signal_rate);
	} while (0);

	if (rc != 0) {
		if (rc == ETIMEDOUT) {
			/* ETIMEDOUT is returned when no SFP is connected */
			if (lm_context->mode != AL_ETH_LM_MODE_DISCONNECTED)
				lm_debug("%s: SFP Disconnected\n", __func__);
			*new_mode = AL_ETH_LM_MODE_DISCONNECTED;
		} else {
			return (rc);
		}
	} else if ((sfp_cable_tech & (SFP_10G_DA_PASSIVE | SFP_10G_DA_ACTIVE)) != 0) {
		if ((signal_rate >= SFP_MIN_SIGNAL_RATE_25G) &&
			((lm_context->max_speed == AL_ETH_LM_MAX_SPEED_25G) ||
			(lm_context->max_speed == AL_ETH_LM_MAX_SPEED_MAX)))
			*new_mode = AL_ETH_LM_MODE_25G;
		else if ((signal_rate >= SFP_MIN_SIGNAL_RATE_10G) &&
			((lm_context->max_speed == AL_ETH_LM_MAX_SPEED_10G) ||
			(lm_context->max_speed == AL_ETH_LM_MAX_SPEED_MAX)))
			*new_mode = AL_ETH_LM_MODE_10G_DA;
		else
			*new_mode = AL_ETH_LM_MODE_1G;

		lm_debug("%s: %s DAC (%d M) detected (max signal rate %d)\n",
			 __func__,
			 (sfp_cable_tech & SFP_10G_DA_PASSIVE) ? "Passive" : "Active",
			  sfp_da_len,
			  signal_rate);

		/* for active direct attached need to use len 0 in the retimer configuration */
		lm_context->da_len = (sfp_cable_tech & SFP_10G_DA_PASSIVE) ? sfp_da_len : 0;
	} else if (sfp_10g != 0) {
		lm_debug("%s: 10 SFP detected\n", __func__);
		*new_mode = AL_ETH_LM_MODE_10G_OPTIC;
	} else if (sfp_1g != 0) {
		lm_debug("%s: 1G SFP detected\n", __func__);
		*new_mode = AL_ETH_LM_MODE_1G;
	} else {
		al_warn("%s: unknown SFP inserted. eeprom content: 10G compliance 0x%x,"
		    " 1G compliance 0x%x, sfp+cable 0x%x. default to %s\n",
		    __func__, sfp_10g, sfp_1g, sfp_cable_tech,
		    al_eth_lm_mode_convert_to_str(lm_context->default_mode));
		*new_mode = lm_context->default_mode;
		lm_context->da_len = lm_context->default_dac_len;
	}

	if ((lm_context->sfp_detect_force_mode) && (*new_mode != AL_ETH_LM_MODE_DISCONNECTED) &&
	    (*new_mode != lm_context->default_mode)) {
		al_warn("%s: Force mode to default (%s). mode based of the SFP EEPROM %s\n",
			__func__, al_eth_lm_mode_convert_to_str(lm_context->default_mode),
			al_eth_lm_mode_convert_to_str(*new_mode));

		*new_mode = lm_context->default_mode;
	}

	lm_context->mode = *new_mode;

	return (0);
}

static int
al_eth_qsfp_detect(struct al_eth_lm_context *lm_context,
    enum al_eth_lm_link_mode *new_mode)
{
	int rc = 0;
	uint8_t qsfp_comp_code;
	uint8_t qsfp_da_len;

	do {
		rc = lm_context->i2c_read(lm_context->i2c_context,
		    lm_context->sfp_bus_id, lm_context->sfp_i2c_addr,
		    QSFP_COMPLIANCE_CODE_IDX, &qsfp_comp_code);
		if (rc != 0)
			break;

		rc = lm_context->i2c_read(lm_context->i2c_context,
		    lm_context->sfp_bus_id, lm_context->sfp_i2c_addr,
		    QSFP_CABLE_LEN_IDX, &qsfp_da_len);
		if (rc != 0)
			break;
	} while (0);

	if (rc != 0) {
		if (rc == ETIMEDOUT) {
			/* ETIMEDOUT is returned when no SFP is connected */
			lm_debug("%s: SFP Disconnected\n", __func__);
			*new_mode = AL_ETH_LM_MODE_DISCONNECTED;
		} else {
			return (rc);
		}
	} else if ((qsfp_comp_code & QSFP_COMPLIANCE_CODE_DAC) != 0) {
		lm_debug("%s: 10G passive DAC (%d M) detected\n",
		    __func__, qsfp_da_len);
		*new_mode = AL_ETH_LM_MODE_10G_DA;
		lm_context->da_len = qsfp_da_len;
	} else if ((qsfp_comp_code & QSFP_COMPLIANCE_CODE_OPTIC) != 0) {
		lm_debug("%s: 10G optic module detected\n", __func__);
		*new_mode = AL_ETH_LM_MODE_10G_OPTIC;
	} else {
		al_warn("%s: unknown QSFP inserted. eeprom content: 10G "
		    "compliance 0x%x default to %s\n", __func__, qsfp_comp_code,
		    al_eth_lm_mode_convert_to_str(lm_context->default_mode));
		*new_mode = lm_context->default_mode;
		lm_context->da_len = lm_context->default_dac_len;
	}

	lm_context->mode = *new_mode;

	return (0);
}

static int
al_eth_module_detect(struct al_eth_lm_context *lm_context,
    enum al_eth_lm_link_mode *new_mode)
{
	int rc = 0;
	uint8_t module_idx;
	int sfp_present = SFP_PRESENT;

	if ((lm_context->gpio_get) && (lm_context->gpio_present != 0))
		sfp_present = lm_context->gpio_get(lm_context->gpio_present);

	if (sfp_present == SFP_NOT_PRESENT) {
		lm_debug("%s: SFP not exist\n", __func__);
		*new_mode = AL_ETH_LM_MODE_DISCONNECTED;

		return 0;
	}

	rc = lm_context->i2c_read(lm_context->i2c_context,
	    lm_context->sfp_bus_id, lm_context->sfp_i2c_addr,
	    MODULE_IDENTIFIER_IDX, &module_idx);
	if (rc != 0) {
		if (rc == ETIMEDOUT) {
			/* ETIMEDOUT is returned when no SFP is connected */
			if (lm_context->mode != AL_ETH_LM_MODE_DISCONNECTED)
				lm_debug("%s: SFP Disconnected\n", __func__);
			*new_mode = AL_ETH_LM_MODE_DISCONNECTED;
			return (0);
		} else {
			return (rc);
		}
	}

	if (module_idx == MODULE_IDENTIFIER_QSFP)
		return (al_eth_qsfp_detect(lm_context, new_mode));
	else
		return (al_eth_sfp_detect(lm_context, new_mode));

	return (0);
}

static struct al_serdes_adv_tx_params da_tx_params = {
	.override		= TRUE,
	.amp			= 0x1,
	.total_driver_units	= 0x13,
	.c_plus_1		= 0x2,
	.c_plus_2		= 0,
	.c_minus_1		= 0x2,
	.slew_rate		= 0,
};

static struct al_serdes_adv_rx_params da_rx_params = {
	.override		= TRUE,
	.dcgain			= 0x4,
	.dfe_3db_freq		= 0x4,
	.dfe_gain		= 0x3,
	.dfe_first_tap_ctrl	= 0x5,
	.dfe_secound_tap_ctrl	= 0x1,
	.dfe_third_tap_ctrl	= 0x8,
	.dfe_fourth_tap_ctrl	= 0x1,
	.low_freq_agc_gain	= 0x7,
	.precal_code_sel	= 0,
	.high_freq_agc_boost	= 0x1d,
};

static struct al_serdes_adv_tx_params optic_tx_params = {
	.override		= TRUE,
	.amp			= 0x1,
	.total_driver_units	= 0x13,
	.c_plus_1		= 0x2,
	.c_plus_2		= 0,
	.c_minus_1		= 0,
	.slew_rate		= 0,
};

static struct al_serdes_adv_rx_params optic_rx_params = {
	.override		= TRUE,
	.dcgain			= 0x0,
	.dfe_3db_freq		= 0x7,
	.dfe_gain		= 0x0,
	.dfe_first_tap_ctrl	= 0x0,
	.dfe_secound_tap_ctrl	= 0x8,
	.dfe_third_tap_ctrl	= 0x0,
	.dfe_fourth_tap_ctrl	= 0x8,
	.low_freq_agc_gain	= 0x7,
	.precal_code_sel	= 0,
	.high_freq_agc_boost	= 0x4,
};

static void
al_eth_serdes_static_tx_params_set(struct al_eth_lm_context *lm_context)
{

	if (lm_context->tx_param_dirty == 0)
		return;

	if (lm_context->serdes_tx_params_valid != 0) {
		lm_context->tx_param_dirty = 0;

		lm_context->tx_params_override.override = TRUE;

		if ((lm_context->serdes_obj->tx_advanced_params_set) == 0) {
			al_err("tx_advanced_params_set is not supported for this serdes group\n");
			return;
		}

		lm_context->serdes_obj->tx_advanced_params_set(
					lm_context->serdes_obj,
					lm_context->lane,
					&lm_context->tx_params_override);

	} else if (lm_context->static_values != 0) {
		lm_context->tx_param_dirty = 0;

		if ((lm_context->serdes_obj->tx_advanced_params_set) == 0) {
			al_err("tx_advanced_params_set is not supported for this serdes group\n");
			return;
		}

		if ((lm_context->retimer_exist == 0) &&
		    (lm_context->mode == AL_ETH_LM_MODE_10G_DA))
			lm_context->serdes_obj->tx_advanced_params_set(
						lm_context->serdes_obj,
						lm_context->lane,
						&da_tx_params);
		else
			lm_context->serdes_obj->tx_advanced_params_set(
						lm_context->serdes_obj,
						lm_context->lane,
						&optic_tx_params);
	}
}

static void
al_eth_serdes_static_rx_params_set(struct al_eth_lm_context *lm_context)
{

	if (lm_context->rx_param_dirty == 0)
		return;

	if (lm_context->serdes_rx_params_valid != 0) {
		lm_context->rx_param_dirty = 0;

		lm_context->rx_params_override.override = TRUE;

		if ((lm_context->serdes_obj->rx_advanced_params_set) == 0) {
			al_err("rx_advanced_params_set is not supported for this serdes group\n");
			return;
		}

		lm_context->serdes_obj->rx_advanced_params_set(
					lm_context->serdes_obj,
					lm_context->lane,
					&lm_context->rx_params_override);


	} else if (lm_context->static_values != 0) {
		lm_context->rx_param_dirty = 0;

		if ((lm_context->serdes_obj->rx_advanced_params_set) == 0) {
			al_err("rx_advanced_params_set is not supported for this serdes group\n");
			return;
		}

		if ((lm_context->retimer_exist == 0) &&
		    (lm_context->mode == AL_ETH_LM_MODE_10G_DA))
			lm_context->serdes_obj->rx_advanced_params_set(
						lm_context->serdes_obj,
						lm_context->lane,
						&da_rx_params);
		else
			lm_context->serdes_obj->rx_advanced_params_set(
						lm_context->serdes_obj,
						lm_context->lane,
						&optic_rx_params);
	}
}

static int
al_eth_rx_equal_run(struct al_eth_lm_context *lm_context)
{
	struct al_serdes_adv_rx_params rx_params;
	int dcgain;
	int best_dcgain = -1;
	int i;
	int best_score  = -1;
	int test_score = -1;

	rx_params.override = FALSE;
	lm_context->serdes_obj->rx_advanced_params_set(lm_context->serdes_obj,
							lm_context->lane, &rx_params);

	lm_debug("score | dcgain | dfe3db | dfegain | tap1 | tap2 | tap3 | "
	    "tap4 | low freq | high freq\n");

	for (dcgain = 0; dcgain < AL_ETH_LM_MAX_DCGAIN; dcgain++) {
		lm_context->serdes_obj->dcgain_set(
					lm_context->serdes_obj,
					dcgain);

		test_score = lm_context->serdes_obj->rx_equalization(
					lm_context->serdes_obj,
					lm_context->lane);

		if (test_score < 0) {
			al_warn("serdes rx equalization failed on error\n");
			return (test_score);
		}

		if (test_score > best_score) {
			best_score = test_score;
			best_dcgain = dcgain;
		}

		lm_context->serdes_obj->rx_advanced_params_get(
					lm_context->serdes_obj,
					lm_context->lane,
					&rx_params);

		lm_debug("%6d|%8x|%8x|%9x|%6x|%6x|%6x|%6x|%10x|%10x|\n",
		    test_score, rx_params.dcgain, rx_params.dfe_3db_freq,
		    rx_params.dfe_gain, rx_params.dfe_first_tap_ctrl,
		    rx_params.dfe_secound_tap_ctrl, rx_params.dfe_third_tap_ctrl,
		    rx_params.dfe_fourth_tap_ctrl, rx_params.low_freq_agc_gain,
		    rx_params.high_freq_agc_boost);
	}

	lm_context->serdes_obj->dcgain_set(
					lm_context->serdes_obj,
					best_dcgain);

	best_score = -1;
	for(i = 0; i < AL_ETH_LM_EQ_ITERATIONS; i++) {
		test_score = lm_context->serdes_obj->rx_equalization(
						lm_context->serdes_obj,
						lm_context->lane);

		if (test_score < 0) {
			al_warn("serdes rx equalization failed on error\n");
			return (test_score);
		}

		if (test_score > best_score) {
			best_score = test_score;
			lm_context->serdes_obj->rx_advanced_params_get(
						lm_context->serdes_obj,
						lm_context->lane,
						&rx_params);
		}
	}

	rx_params.precal_code_sel = 0;
	rx_params.override = TRUE;
	lm_context->serdes_obj->rx_advanced_params_set(
					lm_context->serdes_obj,
					lm_context->lane,
					&rx_params);

	lm_debug("-------------------- best dcgain %d ------------------------------------\n", best_dcgain);
	lm_debug("%6d|%8x|%8x|%9x|%6x|%6x|%6x|%6x|%10x|%10x|\n",
	    best_score, rx_params.dcgain, rx_params.dfe_3db_freq,
	    rx_params.dfe_gain, rx_params.dfe_first_tap_ctrl,
	    rx_params.dfe_secound_tap_ctrl, rx_params.dfe_third_tap_ctrl,
	    rx_params.dfe_fourth_tap_ctrl, rx_params.low_freq_agc_gain,
	    rx_params.high_freq_agc_boost);

	return (0);
}

static int al_eth_lm_retimer_boost_config(struct al_eth_lm_context *lm_context)
{
	int i;
	int rc = 0;
	uint8_t boost = 0;
	uint32_t boost_addr =
	    al_eth_retimer_boost_addr[lm_context->retimer_channel][lm_context->retimer_type];

	if (lm_context->mode != AL_ETH_LM_MODE_10G_DA) {
		boost = al_eth_retimer_boost_value[0][lm_context->retimer_type];
	} else {
		for (i = 0; i < RETIMER_LENS_MAX; i++) {
			if (lm_context->da_len <= al_eth_retimer_boost_lens[i]) {
				boost = al_eth_retimer_boost_value[i][lm_context->retimer_type];
				break;
			}
		}

		if (i == RETIMER_LENS_MAX)
			boost = al_eth_retimer_boost_value[RETIMER_LENS_MAX][lm_context->retimer_type];
	}

	lm_debug("config retimer boost in channel %d (addr %x) to 0x%x\n",
	    lm_context->retimer_channel, boost_addr, boost);

	rc = lm_context->i2c_write(lm_context->i2c_context,
	    lm_context->retimer_bus_id, lm_context->retimer_i2c_addr,
	    boost_addr, boost);

	if (rc != 0) {
		al_err("%s: Error occurred (%d) while writing retimer "
		    "configuration (bus-id %x i2c-addr %x)\n",
		    __func__, rc, lm_context->retimer_bus_id,
		    lm_context->retimer_i2c_addr);
		return (rc);
	}

	return (0);
}

/*******************************************************************************
 ************************** retimer DS25 ***************************************
 ******************************************************************************/
#define LM_DS25_CHANNEL_EN_REG		0xff
#define LM_DS25_CHANNEL_EN_MASK		0x03
#define LM_DS25_CHANNEL_EN_VAL		0x01

#define LM_DS25_CHANNEL_SEL_REG		0xfc
#define LM_DS25_CHANNEL_SEL_MASK	0xff

#define LM_DS25_CDR_RESET_REG		0x0a
#define LM_DS25_CDR_RESET_MASK		0x0c
#define LM_DS25_CDR_RESET_ASSERT	0x0c
#define LM_DS25_CDR_RESET_RELEASE	0x00

#define LM_DS25_SIGNAL_DETECT_REG	0x78
#define LM_DS25_SIGNAL_DETECT_MASK	0x20

#define LM_DS25_CDR_LOCK_REG		0x78
#define LM_DS25_CDR_LOCK_MASK		0x10

#define LM_DS25_DRV_PD_REG		0x15
#define LM_DS25_DRV_PD_MASK		0x08

static int al_eth_lm_retimer_ds25_write_reg(struct al_eth_lm_context	*lm_context,
					    uint8_t			reg_addr,
					    uint8_t			reg_mask,
					    uint8_t			reg_value)
{
	uint8_t reg;
	int rc;

	rc = lm_context->i2c_read(lm_context->i2c_context,
				  lm_context->retimer_bus_id,
				  lm_context->retimer_i2c_addr,
				  reg_addr,
				  &reg);

	if (rc != 0)
		return (EIO);

	reg &= ~(reg_mask);
	reg |= reg_value;

	rc = lm_context->i2c_write(lm_context->i2c_context,
				   lm_context->retimer_bus_id,
				   lm_context->retimer_i2c_addr,
				   reg_addr,
				   reg);

	if (rc != 0)
		return (EIO);

	return (0);
}

static int al_eth_lm_retimer_ds25_channel_select(struct al_eth_lm_context	*lm_context,
						 uint8_t			channel)
{
	int rc = 0;

	/* Write to specific channel */
	rc = al_eth_lm_retimer_ds25_write_reg(lm_context,
					      LM_DS25_CHANNEL_EN_REG,
					      LM_DS25_CHANNEL_EN_MASK,
					      LM_DS25_CHANNEL_EN_VAL);

	if (rc != 0)
		return (rc);

	rc = al_eth_lm_retimer_ds25_write_reg(lm_context,
					      LM_DS25_CHANNEL_SEL_REG,
					      LM_DS25_CHANNEL_SEL_MASK,
					      (1 << channel));

	return (rc);
}

static int al_eth_lm_retimer_ds25_channel_config(struct al_eth_lm_context	*lm_context,
						 uint8_t			channel,
						 struct retimer_config_reg	*config,
						 uint8_t			config_size)
{
	uint8_t i;
	int rc;

	rc = al_eth_lm_retimer_ds25_channel_select(lm_context, channel);
	if (rc != 0)
		goto config_error;

	for (i = 0; i < config_size; i++) {
		rc = al_eth_lm_retimer_ds25_write_reg(lm_context,
						      config[i].addr,
						      config[i].mask,
						      config[i].value);

		if (rc != 0)
			goto config_error;
	}

	lm_debug("%s: retimer channel config done for channel %d\n", __func__, channel);

	return (0);

config_error:
	al_err("%s: failed to access to the retimer\n", __func__);

	return (rc);
}

static int al_eth_lm_retimer_ds25_cdr_reset(struct al_eth_lm_context *lm_context, uint32_t channel)
{
	int rc;

	lm_debug("Perform CDR reset to channel %d\n", channel);

	rc = al_eth_lm_retimer_ds25_channel_select(lm_context, channel);
	if (rc)
		goto config_error;

	rc = al_eth_lm_retimer_ds25_write_reg(lm_context,
					      LM_DS25_CDR_RESET_REG,
					      LM_DS25_CDR_RESET_MASK,
					      LM_DS25_CDR_RESET_ASSERT);

	if (rc)
		goto config_error;

	rc = al_eth_lm_retimer_ds25_write_reg(lm_context,
					      LM_DS25_CDR_RESET_REG,
					      LM_DS25_CDR_RESET_MASK,
					      LM_DS25_CDR_RESET_RELEASE);

	if (rc)
		goto config_error;

	return 0;

config_error:
	al_err("%s: failed to access to the retimer\n", __func__);

	return rc;
}

static boolean_t al_eth_lm_retimer_ds25_signal_detect(struct al_eth_lm_context *lm_context,
						    uint32_t channel)
{
	int rc = 0;
	uint8_t reg;

	rc = al_eth_lm_retimer_ds25_channel_select(lm_context, channel);
	if (rc)
		goto config_error;

	rc = lm_context->i2c_read(lm_context->i2c_context,
				  lm_context->retimer_bus_id,
				  lm_context->retimer_i2c_addr,
				  LM_DS25_SIGNAL_DETECT_REG,
				  &reg);

	if (rc)
		goto config_error;

	if (reg & LM_DS25_SIGNAL_DETECT_MASK)
		return TRUE;

	return FALSE;

config_error:
	al_err("%s: failed to access to the retimer\n", __func__);

	return FALSE;
}

static boolean_t al_eth_lm_retimer_ds25_cdr_lock(struct al_eth_lm_context *lm_context,
					       uint32_t channel)
{
	int rc = 0;
	uint8_t reg;

	rc = al_eth_lm_retimer_ds25_channel_select(lm_context, channel);
	if (rc)
		goto config_error;

	rc = lm_context->i2c_read(lm_context->i2c_context,
				  lm_context->retimer_bus_id,
				  lm_context->retimer_i2c_addr,
				  LM_DS25_CDR_LOCK_REG,
				  &reg);

	if (rc)
		goto config_error;

	if (reg & LM_DS25_CDR_LOCK_MASK)
		return TRUE;

	return FALSE;

config_error:
	al_err("%s: failed to access to the retimer\n", __func__);

	return FALSE;
}

static boolean_t al_eth_lm_wait_for_lock(struct al_eth_lm_context	*lm_context,
				       uint32_t			channel)
{
	uint32_t timeout = AL_ETH_LM_RETIMER_WAIT_FOR_LOCK;
	al_bool lock = AL_FALSE;

	while ((timeout > 0) && (lock == FALSE)) {
		al_msleep(10);
		timeout -= 10;

		lock = retimer[lm_context->retimer_type].cdr_lock(lm_context, channel);
	}

	lm_debug("%s: %s to achieve CDR lock in %d msec\n",
		 __func__, (lock) ? "succeed" : "FAILED",
		 (AL_ETH_LM_RETIMER_WAIT_FOR_LOCK - timeout));

	return lock;
}

static void al_eth_lm_retimer_signal_lock_check(struct al_eth_lm_context	*lm_context,
						uint32_t			channel,
						boolean_t			*ready)
{
	al_bool signal_detect = TRUE;
	al_bool cdr_lock = TRUE;

	if (retimer[lm_context->retimer_type].signal_detect) {
		if (!retimer[lm_context->retimer_type].signal_detect(lm_context, channel)) {
			lm_debug("no signal detected on retimer channel %d\n", channel);

			signal_detect = AL_FALSE;
		} else {
			if (retimer[lm_context->retimer_type].cdr_lock) {
				cdr_lock = retimer[lm_context->retimer_type].cdr_lock(
									lm_context,
									channel);
				if (!cdr_lock) {
					if (retimer[lm_context->retimer_type].reset) {
						retimer[lm_context->retimer_type].reset(lm_context,
											channel);

						cdr_lock = al_eth_lm_wait_for_lock(lm_context,
										   channel);
					}
				}
			}
		}
	}

	al_info("%s: (channel %d) signal %d cdr lock %d\n",
		 __func__, channel, signal_detect, (signal_detect) ? cdr_lock : 0);

	*ready = ((cdr_lock == TRUE) && (signal_detect == TRUE));
}

static int al_eth_lm_retimer_ds25_full_config(struct al_eth_lm_context *lm_context)
{
	int rc = 0;
	al_bool ready;
	struct retimer_config_reg *config_tx;
	uint32_t config_tx_size;
	struct retimer_config_reg *config_rx;
	uint32_t config_rx_size;

	if (lm_context->mode == AL_ETH_LM_MODE_25G) {
		config_tx = retimer_ds25_25g_mode_tx_ch;
		config_tx_size = AL_ARR_SIZE(retimer_ds25_25g_mode_tx_ch);

		config_rx = retimer_ds25_25g_mode_rx_ch;
		config_rx_size = AL_ARR_SIZE(retimer_ds25_25g_mode_rx_ch);

	} else {
		config_tx = retimer_ds25_10g_mode;
		config_tx_size = AL_ARR_SIZE(retimer_ds25_10g_mode);

		config_rx = retimer_ds25_10g_mode;
		config_rx_size = AL_ARR_SIZE(retimer_ds25_10g_mode);
	}


	rc = al_eth_lm_retimer_ds25_channel_config(lm_context,
					lm_context->retimer_channel,
					config_rx,
					config_rx_size);

	if (rc)
		return rc;

	rc = al_eth_lm_retimer_ds25_channel_config(lm_context,
					lm_context->retimer_tx_channel,
					config_tx,
					config_tx_size);

	if (rc)
		return rc;

	if (lm_context->serdes_obj->type_get() == AL_SRDS_TYPE_25G) {
		lm_debug("%s: serdes 25G - perform tx and rx gearbox reset\n", __func__);
		al_eth_gearbox_reset(lm_context->adapter, TRUE, TRUE);
		DELAY(AL_ETH_LM_GEARBOX_RESET_DELAY);
	}

	al_eth_lm_retimer_signal_lock_check(lm_context, lm_context->retimer_tx_channel, &ready);

	if (!ready) {
		lm_debug("%s: Failed to lock tx channel!\n", __func__);
		return (1);
	}

	lm_debug("%s: retimer full configuration done\n", __func__);

	return rc;
}

static int al_eth_lm_retimer_25g_rx_adaptation(struct al_eth_lm_context *lm_context)
{
	int rc = 0;
	al_bool ready;

	al_eth_lm_retimer_signal_lock_check(lm_context, lm_context->retimer_channel, &ready);

	if (!ready) {
		lm_debug("%s: no signal detected on retimer Rx channel (%d)\n",
			 __func__,  lm_context->retimer_channel);

		return rc;
	}

	al_msleep(AL_ETH_LM_SERDES_WAIT_FOR_LOCK);

	return 0;
}

static int al_eth_lm_check_for_link(struct al_eth_lm_context *lm_context, boolean_t *link_up)
{
	struct al_eth_link_status status;
	int ret = 0;

	al_eth_link_status_clear(lm_context->adapter);
	al_eth_link_status_get(lm_context->adapter, &status);

	if (status.link_up == AL_TRUE) {
		lm_debug("%s: >>>> Link state DOWN ==> UP\n", __func__);
		al_eth_led_set(lm_context->adapter, AL_TRUE);
		lm_context->link_state = AL_ETH_LM_LINK_UP;
		*link_up = AL_TRUE;

		return 0;
	} else if (status.local_fault) {
		lm_context->link_state = AL_ETH_LM_LINK_DOWN;
		al_eth_led_set(lm_context->adapter, AL_FALSE);

		al_err("%s: Failed to establish link\n", __func__);
		ret = 1;
	} else {
		lm_debug("%s: >>>> Link state DOWN ==> DOWN_RF\n", __func__);
		lm_context->link_state = AL_ETH_LM_LINK_DOWN_RF;
		al_eth_led_set(lm_context->adapter, AL_FALSE);

		ret = 0;
	}

	*link_up = AL_FALSE;
	return ret;
}

/*****************************************************************************/
/***************************** API functions *********************************/
/*****************************************************************************/
int
al_eth_lm_init(struct al_eth_lm_context	*lm_context,
    struct al_eth_lm_init_params *params)
{

	lm_context->adapter = params->adapter;
	lm_context->serdes_obj = params->serdes_obj;
	lm_context->lane = params->lane;
	lm_context->sfp_detection = params->sfp_detection;
	lm_context->sfp_bus_id = params->sfp_bus_id;
	lm_context->sfp_i2c_addr = params->sfp_i2c_addr;

	lm_context->retimer_exist = params->retimer_exist;
	lm_context->retimer_type = params->retimer_type;
	lm_context->retimer_bus_id = params->retimer_bus_id;
	lm_context->retimer_i2c_addr = params->retimer_i2c_addr;
	lm_context->retimer_channel = params->retimer_channel;
	lm_context->retimer_tx_channel = params->retimer_tx_channel;

	lm_context->default_mode = params->default_mode;
	lm_context->default_dac_len = params->default_dac_len;
	lm_context->link_training = params->link_training;
	lm_context->rx_equal = params->rx_equal;
	lm_context->static_values = params->static_values;
	lm_context->i2c_read = params->i2c_read;
	lm_context->i2c_write = params->i2c_write;
	lm_context->i2c_context = params->i2c_context;
	lm_context->get_random_byte = params->get_random_byte;

	/* eeprom_read must be provided if sfp_detection is true */
	al_assert((lm_context->sfp_detection == FALSE) ||
	    (lm_context->i2c_read != NULL));

	al_assert((lm_context->retimer_exist == FALSE) ||
	    (lm_context->i2c_write != NULL));

	lm_context->local_adv.selector_field = 1;
	lm_context->local_adv.capability = 0;
	lm_context->local_adv.remote_fault = 0;
	lm_context->local_adv.acknowledge = 0;
	lm_context->local_adv.next_page = 0;
	lm_context->local_adv.technology = AL_ETH_AN_TECH_10GBASE_KR;
	lm_context->local_adv.fec_capability = params->kr_fec_enable;

	lm_context->mode = AL_ETH_LM_MODE_DISCONNECTED;
	lm_context->serdes_tx_params_valid = FALSE;
	lm_context->serdes_rx_params_valid = FALSE;

	lm_context->rx_param_dirty = 1;
	lm_context->tx_param_dirty = 1;

	lm_context->gpio_get = params->gpio_get;
	lm_context->gpio_present = params->gpio_present;

	lm_context->max_speed = params->max_speed;
	lm_context->sfp_detect_force_mode = params->sfp_detect_force_mode;

	lm_context->lm_pause = params->lm_pause;

	lm_context->led_config = params->led_config;

	lm_context->retimer_configured = FALSE;

	lm_context->link_state = AL_ETH_LM_LINK_DOWN;

	return (0);
}

int
al_eth_lm_link_detection(struct al_eth_lm_context *lm_context,
    boolean_t *link_fault, enum al_eth_lm_link_mode *old_mode,
    enum al_eth_lm_link_mode *new_mode)
{
	int err;
	struct al_eth_link_status status;

	al_assert(lm_context != NULL);
	al_assert(old_mode != NULL);
	al_assert(new_mode != NULL);

	/**
	 * if Link management is disabled, report no link fault in case the link was up
	 * before and set new mode to disconnected to avoid calling to link establish
	 * if the link wasn't up.
	 */
	if (lm_context->lm_pause != NULL) {
		boolean_t lm_pause = lm_context->lm_pause(lm_context->i2c_context);
		if (lm_pause == TRUE) {
			*new_mode = AL_ETH_LM_MODE_DISCONNECTED;
			if (link_fault != NULL) {
				if (lm_context->link_state == AL_ETH_LM_LINK_UP)
					*link_fault = FALSE;
				else
					*link_fault = TRUE;
			}

			return 0;
		}
	}

	*old_mode = lm_context->mode;
	*new_mode = lm_context->mode;

	if (link_fault != NULL)
		*link_fault = TRUE;

	switch (lm_context->link_state) {
	case AL_ETH_LM_LINK_UP:
		al_eth_link_status_get(lm_context->adapter, &status);

		if (status.link_up) {
			if (link_fault != NULL)
				*link_fault = FALSE;

			al_eth_led_set(lm_context->adapter, TRUE);

			return (0);
		} else if (status.local_fault) {
			lm_debug("%s: >>>> Link state UP ==> DOWN\n", __func__);
			lm_context->link_state = AL_ETH_LM_LINK_DOWN;
		} else {
			lm_debug("%s: >>>> Link state UP ==> DOWN_RF\n", __func__);
			lm_context->link_state = AL_ETH_LM_LINK_DOWN_RF;
		}

		break;
	case AL_ETH_LM_LINK_DOWN_RF:
		al_eth_link_status_get(lm_context->adapter, &status);

		if (status.local_fault) {
			lm_debug("%s: >>>> Link state DOWN_RF ==> DOWN\n", __func__);
			lm_context->link_state = AL_ETH_LM_LINK_DOWN;

			break;
		} else if (status.remote_fault == FALSE) {
			lm_debug("%s: >>>> Link state DOWN_RF ==> UP\n", __func__);
			lm_context->link_state = AL_ETH_LM_LINK_UP;
		}
		/* in case of remote fault only no need to check SFP again */
		return (0);
	case AL_ETH_LM_LINK_DOWN:
		break;
	};

	al_eth_led_set(lm_context->adapter, FALSE);

	if (lm_context->sfp_detection) {
		err = al_eth_module_detect(lm_context, new_mode);
		if (err != 0) {
			al_err("module_detection failed!\n");
			return (err);
		}

		lm_context->mode = *new_mode;
	} else {
		lm_context->mode = lm_context->default_mode;
		*new_mode = lm_context->mode;
	}

	if (*old_mode != *new_mode) {
		al_info("%s: New SFP mode detected %s -> %s\n",
		    __func__, al_eth_lm_mode_convert_to_str(*old_mode),
		    al_eth_lm_mode_convert_to_str(*new_mode));

		lm_context->rx_param_dirty = 1;
		lm_context->tx_param_dirty = 1;

		lm_context->new_port = TRUE;

		if ((*new_mode != AL_ETH_LM_MODE_DISCONNECTED) && (lm_context->led_config)) {
			struct al_eth_lm_led_config_data data = {0};

			switch (*new_mode) {
			case AL_ETH_LM_MODE_10G_OPTIC:
			case AL_ETH_LM_MODE_10G_DA:
				data.speed = AL_ETH_LM_LED_CONFIG_10G;
				break;
			case AL_ETH_LM_MODE_1G:
				data.speed = AL_ETH_LM_LED_CONFIG_1G;
				break;
			case AL_ETH_LM_MODE_25G:
				data.speed = AL_ETH_LM_LED_CONFIG_25G;
				break;
			default:
				al_err("%s: unknown LM mode!\n", __func__);
			};

			lm_context->led_config(lm_context->i2c_context, &data);
		}
	}

	return (0);
}

int
al_eth_lm_link_establish(struct al_eth_lm_context *lm_context, boolean_t *link_up)
{
	boolean_t signal_detected;
	int ret = 0;

	switch (lm_context->link_state) {
	case AL_ETH_LM_LINK_UP:
		*link_up = TRUE;
		lm_debug("%s: return link up\n", __func__);

		return (0);
	case AL_ETH_LM_LINK_DOWN_RF:
		*link_up = FALSE;
		lm_debug("%s: return link down (DOWN_RF)\n", __func__);

		return (0);
	case AL_ETH_LM_LINK_DOWN:
		break;
	};

	/**
	 * At this point we will get LM disable only if changed to disable after link detection
	 * finished. in this case link will not be established until LM will be enable again.
	 */
	if (lm_context->lm_pause) {
		boolean_t lm_pause = lm_context->lm_pause(lm_context->i2c_context);
		if (lm_pause == TRUE) {
			*link_up = FALSE;

			return (0);
		}
	}

	if ((lm_context->new_port) && (lm_context->retimer_exist)) {
		al_eth_serdes_static_rx_params_set(lm_context);
		al_eth_serdes_static_tx_params_set(lm_context);
#if 0
		al_eth_lm_retimer_config(lm_context);
		DELAY(AL_ETH_LM_RETIMER_LINK_STATUS_DELAY);
#endif

		if (retimer[lm_context->retimer_type].config(lm_context)) {
			al_info("%s: failed to configure the retimer\n", __func__);

			*link_up = FALSE;
			return (1);
		}

		lm_context->new_port = FALSE;

		DELAY(1000);
	}

	if (lm_context->retimer_exist) {
		if (retimer[lm_context->retimer_type].rx_adaptation) {
			ret = retimer[lm_context->retimer_type].rx_adaptation(lm_context);

			if (ret != 0) {
				lm_debug("retimer rx is not ready\n");
				*link_up = FALSE;

				return (0);
			}
		}
	}

	signal_detected = lm_context->serdes_obj->signal_is_detected(
					lm_context->serdes_obj,
					lm_context->lane);

	if (signal_detected == FALSE) {
		/* if no signal detected there is nothing to do */
		lm_debug("serdes signal is down\n");
		*link_up = AL_FALSE;
		return 0;
	}

	if (lm_context->serdes_obj->type_get() == AL_SRDS_TYPE_25G) {
		lm_debug("%s: serdes 25G - perform rx gearbox reset\n", __func__);
		al_eth_gearbox_reset(lm_context->adapter, FALSE, TRUE);
		DELAY(AL_ETH_LM_GEARBOX_RESET_DELAY);
	}


	if (lm_context->retimer_exist) {
		DELAY(AL_ETH_LM_RETIMER_LINK_STATUS_DELAY);

		ret = al_eth_lm_check_for_link(lm_context, link_up);

		if (ret == 0) {
			lm_debug("%s: link is up with retimer\n", __func__);
			return 0;
		}

		return ret;
	}

	if ((lm_context->mode == AL_ETH_LM_MODE_10G_DA) && (lm_context->link_training)) {
		lm_context->local_adv.transmitted_nonce = lm_context->get_random_byte();
		lm_context->local_adv.transmitted_nonce &= 0x1f;

		ret = al_eth_an_lt_execute(lm_context->adapter,
					   lm_context->serdes_obj,
					   lm_context->lane,
					   &lm_context->local_adv,
					   &lm_context->partner_adv);

		lm_context->rx_param_dirty = 1;
		lm_context->tx_param_dirty = 1;

		if (ret == 0) {
			al_info("%s: link training finished successfully\n", __func__);
			lm_context->link_training_failures = 0;
			ret = al_eth_lm_check_for_link(lm_context, link_up);

			if (ret == 0) {
				lm_debug("%s: link is up with LT\n", __func__);
				return (0);
			}

		}

		lm_context->link_training_failures++;
		if (lm_context->link_training_failures > AL_ETH_LT_FAILURES_TO_RESET) {
			lm_debug("%s: failed to establish LT %d times. reset serdes\n",
				 __func__, AL_ETH_LT_FAILURES_TO_RESET);

			lm_context->serdes_obj->pma_hard_reset_lane(
						lm_context->serdes_obj,
						lm_context->lane,
						TRUE);
			lm_context->serdes_obj->pma_hard_reset_lane(
						lm_context->serdes_obj,
						lm_context->lane,
						FALSE);
			lm_context->link_training_failures = 0;
		}
	}

	al_eth_serdes_static_tx_params_set(lm_context);

	if ((lm_context->mode == AL_ETH_LM_MODE_10G_DA) &&
	    (lm_context->rx_equal)) {
		ret = al_eth_rx_equal_run(lm_context);

		if (ret == 0) {
			DELAY(AL_ETH_LM_LINK_STATUS_DELAY);
			ret = al_eth_lm_check_for_link(lm_context, link_up);

			if (ret == 0) {
				lm_debug("%s: link is up with Rx Equalization\n", __func__);
				return (0);
			}
		}
	}

	al_eth_serdes_static_rx_params_set(lm_context);

	DELAY(AL_ETH_LM_LINK_STATUS_DELAY);

	ret = al_eth_lm_check_for_link(lm_context, link_up);

	if (ret == 0) {
		lm_debug("%s: link is up with static parameters\n", __func__);
		return (0);
	}

	*link_up = FALSE;
	return (1);
}

int
al_eth_lm_static_parameters_override(struct al_eth_lm_context *lm_context,
    struct al_serdes_adv_tx_params *tx_params,
    struct al_serdes_adv_rx_params *rx_params)
{

	if (tx_params != NULL) {
		lm_context->tx_params_override = *tx_params;
		lm_context->tx_param_dirty = 1;
		lm_context->serdes_tx_params_valid = TRUE;
	}

	if (rx_params != NULL) {
		lm_context->rx_params_override = *rx_params;
		lm_context->rx_param_dirty = 1;
		lm_context->serdes_rx_params_valid = TRUE;
	}

	return (0);
}

int
al_eth_lm_static_parameters_override_disable(struct al_eth_lm_context *lm_context,
    boolean_t tx_params, boolean_t rx_params)
{

	if (tx_params != 0)
		lm_context->serdes_tx_params_valid = FALSE;
	if (rx_params != 0)
		lm_context->serdes_tx_params_valid = FALSE;

	return (0);
}

int
al_eth_lm_static_parameters_get(struct al_eth_lm_context *lm_context,
    struct al_serdes_adv_tx_params *tx_params,
    struct al_serdes_adv_rx_params *rx_params)
{

	if (tx_params != NULL) {
		if (lm_context->serdes_tx_params_valid)
			*tx_params = lm_context->tx_params_override;
		else
			lm_context->serdes_obj->tx_advanced_params_get(
							lm_context->serdes_obj,
							lm_context->lane,
							tx_params);
	}

	if (rx_params != NULL) {
		if (lm_context->serdes_rx_params_valid)
			*rx_params = lm_context->rx_params_override;
		else
			lm_context->serdes_obj->rx_advanced_params_get(
							lm_context->serdes_obj,
							lm_context->lane,
							rx_params);
	}

	return (0);
}

const char *
al_eth_lm_mode_convert_to_str(enum al_eth_lm_link_mode val)
{

	switch (val) {
	case AL_ETH_LM_MODE_DISCONNECTED:
		return ("AL_ETH_LM_MODE_DISCONNECTED");
	case AL_ETH_LM_MODE_10G_OPTIC:
		return ("AL_ETH_LM_MODE_10G_OPTIC");
	case AL_ETH_LM_MODE_10G_DA:
		return ("AL_ETH_LM_MODE_10G_DA");
	case AL_ETH_LM_MODE_1G:
		return ("AL_ETH_LM_MODE_1G");
	case AL_ETH_LM_MODE_25G:
		return ("AL_ETH_LM_MODE_25G");
	}

	return ("N/A");
}

void
al_eth_lm_debug_mode_set(struct al_eth_lm_context *lm_context,
    boolean_t enable)
{

	lm_context->debug = enable;
}
