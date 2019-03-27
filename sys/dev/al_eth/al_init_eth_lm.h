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
 *
 * $FreeBSD$
 */

/**
 *  Ethernet
 *  @{
 * @file   al_init_eth_lm.h
 *
 * @brief ethernet link management common utilities
 *
 * Common operation example:
 * @code
 *      int main()
 *      {
 *		struct al_eth_lm_context lm_context;
 *		struct al_eth_lm_init_params lm_params;
 *		enum al_eth_lm_link_mode old_mode;
 *		enum al_eth_lm_link_mode new_mode;
 *		al_bool	fault;
 *		al_bool				link_up;
 *		int rc = 0;
 *
 *		lm_params.adapter = hal_adapter;
 *		lm_params.serdes_obj = serdes;
 *		lm_params.grp = grp;
 *		lm_params.lane = lane;
 *		lm_params.sfp_detection = true;
 *		lm_params.link_training = true;
 *		lm_params.rx_equal = true
 *		lm_params.static_values = true;
 *		lm_params.kr_fec_enable = false;
 *		lm_params.eeprom_read = &my_eeprom_read;
 *		lm_params.eeprom_context = context;
 *		lm_params.get_random_byte = &my_rand_byte;
 *		lm_params.default_mode = AL_ETH_LM_MODE_10G_DA;
 *
 *		al_eth_lm_init(&lm_context, &lm_params);
 *
 *		rc = al_eth_lm_link_detection(&lm_context, &fault, &old_mode, &new_mode);
 *		if (fault == false)
 *			return; // in this case the link is still up
 *
 *		if (rc) {
 *			printf("link detection failed on error\n");
 *			return;
 *		}
 *
 *		if (old_mode != new_mode) {
 *			 // perform serdes configuration if needed
 *
 *			 // mac stop / start / config if needed
 *		}
 *
 *		spin_lock(lock);
 *		rc = al_eth_lm_link_establish($lm_context, &link_up);
 *		spin_unlock(lock);
 *		if (rc) {
 *			printf("establish link failed\n");
 *			return;
 *		}
 *
 *		if (link_up)
 *			printf("Link established successfully\n");
 *		else
 *			printf("No signal found. probably the link partner is disconnected\n");
 *      }
 * @endcode
 *
 */

#ifndef __AL_INIT_ETH_LM_H__
#define __AL_INIT_ETH_LM_H__

#include <al_serdes.h>
#include <al_hal_eth.h>
#include "al_init_eth_kr.h"

enum al_eth_lm_link_mode {
	AL_ETH_LM_MODE_DISCONNECTED,
	AL_ETH_LM_MODE_10G_OPTIC,
	AL_ETH_LM_MODE_10G_DA,
	AL_ETH_LM_MODE_1G,
	AL_ETH_LM_MODE_25G,
};

enum al_eth_lm_max_speed {
	AL_ETH_LM_MAX_SPEED_MAX,
	AL_ETH_LM_MAX_SPEED_25G,
	AL_ETH_LM_MAX_SPEED_10G,
	AL_ETH_LM_MAX_SPEED_1G,
};

enum al_eth_lm_link_state {
	AL_ETH_LM_LINK_DOWN,
	AL_ETH_LM_LINK_DOWN_RF,
	AL_ETH_LM_LINK_UP,
};

enum al_eth_lm_led_config_speed {
	AL_ETH_LM_LED_CONFIG_1G,
	AL_ETH_LM_LED_CONFIG_10G,
	AL_ETH_LM_LED_CONFIG_25G,
};

struct al_eth_lm_led_config_data {
	enum al_eth_lm_led_config_speed	speed;
};


struct al_eth_lm_context {
	struct al_hal_eth_adapter	*adapter;
	struct al_serdes_grp_obj	*serdes_obj;
	enum al_serdes_lane		lane;

	uint32_t			link_training_failures;

	boolean_t			tx_param_dirty;
	boolean_t			serdes_tx_params_valid;
	struct al_serdes_adv_tx_params	tx_params_override;
	boolean_t			rx_param_dirty;
	boolean_t			serdes_rx_params_valid;
	struct al_serdes_adv_rx_params	rx_params_override;

	struct al_eth_an_adv		local_adv;
	struct al_eth_an_adv		partner_adv;

	enum al_eth_lm_link_mode	mode;
	uint8_t				da_len;
	boolean_t			debug;

	/* configurations */
	boolean_t			sfp_detection;
	uint8_t				sfp_bus_id;
	uint8_t				sfp_i2c_addr;

	enum al_eth_lm_link_mode	default_mode;
	uint8_t				default_dac_len;
	boolean_t			link_training;
	boolean_t			rx_equal;
	boolean_t			static_values;

	boolean_t			retimer_exist;
	enum al_eth_retimer_type	retimer_type;
	uint8_t				retimer_bus_id;
	uint8_t				retimer_i2c_addr;
	enum al_eth_retimer_channel	retimer_channel;

	/* services */
	int (*i2c_read)(void *handle, uint8_t bus_id, uint8_t i2c_addr,
	    uint8_t reg_addr, uint8_t *val);
	int (*i2c_write)(void *handle, uint8_t bus_id, uint8_t i2c_addr,
	    uint8_t reg_addr, uint8_t val);
	void *i2c_context;
	uint8_t (*get_random_byte)(void);

	int (*gpio_get)(unsigned int gpio);
	uint32_t			gpio_present;

	enum al_eth_retimer_channel	retimer_tx_channel;
	boolean_t			retimer_configured;

	enum al_eth_lm_max_speed	max_speed;

	boolean_t			sfp_detect_force_mode;

	enum al_eth_lm_link_state	link_state;
	boolean_t			new_port;

	boolean_t (*lm_pause)(void *handle);

	void (*led_config)(void *handle, struct al_eth_lm_led_config_data *data);
};

struct al_eth_lm_init_params {
	/* pointer to HAL context */
	struct al_hal_eth_adapter	*adapter;
	/* pointer to serdes object */
	struct al_serdes_grp_obj	*serdes_obj;
	/* serdes lane for this port */
	enum al_serdes_lane		lane;

	/*
	 * set to true to perform sfp detection if the link is down.
	 * when set to true, eeprom_read below should NOT be NULL.
	 */
	boolean_t			sfp_detection;
	/* i2c bus id of the SFP for this port */
	uint8_t				sfp_bus_id;
	/* i2c addr of the SFP for this port */
	uint8_t				sfp_i2c_addr;
	/*
	 * default mode, and dac length will be used in case sfp_detection
	 * is not set or in case the detection failed.
	 */
	enum al_eth_lm_link_mode	default_mode;
	uint8_t				default_dac_len;

	/* the i2c bus id and addr of the retimer in case it exist */
	uint8_t				retimer_bus_id;
	uint8_t				retimer_i2c_addr;
	/* retimer channel connected to this port */
	enum al_eth_retimer_channel	retimer_channel;
	enum al_eth_retimer_channel	retimer_tx_channel;
	/* retimer type if exist */
	enum al_eth_retimer_type	retimer_type;

	/*
	 * the following parameters control what mechanisms to run
	 * on link_establish with the following steps:
	 * - if retimer_exist is set, the retimer will be configured based on DA len.
	 * - if link_training is set and DA detected run link training. if succeed return 0
	 * - if rx_equal is set serdes equalization will be run to configure the rx parameters.
	 * - if static_values is set, tx and rx values will be set based on static values.
	 */
	boolean_t			retimer_exist;
	boolean_t			link_training;
	boolean_t			rx_equal;
	boolean_t			static_values;

	/* enable / disable fec capabilities in AN */
	boolean_t			kr_fec_enable;

	/*
	 * pointer to function that's read 1 byte from eeprom
	 * in case no eeprom is connected should return -ETIMEDOUT
	 */
	int (*i2c_read)(void *handle, uint8_t bus_id, uint8_t i2c_addr,
	    uint8_t reg_addr, uint8_t *val);
	int (*i2c_write)(void *handle, uint8_t bus_id, uint8_t i2c_addr,
	    uint8_t reg_addr, uint8_t val);
	void *i2c_context;
	/* pointer to function that return 1 rand byte */
	uint8_t (*get_random_byte)(void);

	/* pointer to function that gets GPIO value - if NULL gpio present won't be used */
	int (*gpio_get)(unsigned int gpio);
	/* gpio number connected to the SFP present pin */
	uint32_t			gpio_present;

	enum al_eth_lm_max_speed	max_speed;

	/* in case force mode is true - the default mode will be set regardless to
	 * the SFP EEPROM content */
	boolean_t			sfp_detect_force_mode;

	/* lm pause callback - in case it return true the LM will try to preserve
	 * the current link status and will not try to establish new link (and will not
	 * access to i2c bus) */
	boolean_t (*lm_pause)(void *handle);

	/* config ethernet LEDs according to data. can be NULL if no configuration needed */
	void (*led_config)(void *handle, struct al_eth_lm_led_config_data *data);
};

/**
 * initialize link management context and set configuration
 *
 * @param  lm_context pointer to link management context
 * @param  params  parameters passed from upper layer
 *
 * @return 0 in case of success. otherwise on failure.
 */
int al_eth_lm_init(struct al_eth_lm_context *lm_context,
    struct al_eth_lm_init_params *params);

/**
 * perform link status check. in case link is down perform sfp detection
 *
 * @param lm_context pointer to link management context
 * @param link_fault indicate if the link is down
 * @param old_mode the last working mode
 * @param new_mode the new mode detected in this call
 *
 * @return  0 in case of success. otherwise on failure.
 */
int al_eth_lm_link_detection(struct al_eth_lm_context *lm_context,
    boolean_t *link_fault, enum al_eth_lm_link_mode *old_mode,
    enum al_eth_lm_link_mode *new_mode);

/**
 * run LT, rx equalization and static values override according to configuration
 * This function MUST be called inside a lock as it using common serdes registers
 *
 * @param lm_context pointer to link management context
 * @param link_up set to true in case link is establish successfully
 *
 * @return < 0 in case link was failed to be established
 */
int al_eth_lm_link_establish(struct al_eth_lm_context *lm_context,
    boolean_t *link_up);

/**
 * override the default static parameters
 *
 * @param lm_context pointer to link management context
 * @param tx_params pointer to new tx params
 * @param rx_params pointer to new rx params
 *
 * @return  0 in case of success. otherwise on failure.
 **/
int al_eth_lm_static_parameters_override(struct al_eth_lm_context *lm_context,
    struct al_serdes_adv_tx_params *tx_params,
    struct al_serdes_adv_rx_params *rx_params);

/**
 * disable serdes parameters override
 *
 * @param lm_context pointer to link management context
 * @param tx_params set to true to disable override of tx params
 * @param rx_params set to true to disable override of rx params
 *
 * @return  0 in case of success. otherwise on failure.
 **/
int al_eth_lm_static_parameters_override_disable(struct al_eth_lm_context *lm_context,
   boolean_t tx_params, boolean_t rx_params);

/**
 * get the static parameters that are being used
 * if the parameters was override - return the override values
 * else return the current values of the parameters
 *
 * @param  lm_context pointer to link management context
 * @param  tx_params  pointer to new tx params
 * @param  rx_params  pointer to new rx params
 *
 * @return  0 in case of success. otherwise on failure.
 */
int al_eth_lm_static_parameters_get(struct al_eth_lm_context *lm_context,
    struct al_serdes_adv_tx_params *tx_params,
    struct al_serdes_adv_rx_params *rx_params);

/**
 * convert link management mode to string
 *
 * @param  val link management mode
 *
 * @return     string of the mode
 */
const char *al_eth_lm_mode_convert_to_str(enum al_eth_lm_link_mode val);

/**
 * print all debug messages
 *
 * @param lm_context pointer to link management context
 * @param enable     set to true to enable debug mode
 */
void al_eth_lm_debug_mode_set(struct al_eth_lm_context *lm_context,
    boolean_t enable);
#endif
