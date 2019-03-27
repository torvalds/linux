/*-
*******************************************************************************
Copyright (C) 2015 Annapurna Labs Ltd.

This file may be licensed under the terms of the Annapurna Labs Commercial
License Agreement.

Alternatively, this file can be distributed under the terms of the GNU General
Public License V2 as published by the Free Software Foundation and can be
found at http://www.gnu.org/licenses/gpl-2.0.html

Alternatively, redistribution and use in source and binary forms, with or
without modification, are permitted provided that the following conditions are
met:

    *     Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

    *     Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in
the documentation and/or other materials provided with the
distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*******************************************************************************/
/**
 * @defgroup group_eth_kr_api API
 * Ethernet KR auto-neg and link-training driver API
 * @ingroup group_eth
 * @{
 * @file   al_hal_eth_kr.h
 *
 * @brief Header file for KR driver
 *
 *
 */

#ifndef __AL_HAL_ETH_KR_H__
#define __AL_HAL_ETH_KR_H__

#include "al_hal_eth.h"

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

/* AN (Auto-negotiation) Advertisement Registers */
struct al_eth_an_adv {
	/* constant value defining 802.3ap support.
	 * The suggested value is 0x01.*/
	uint8_t   selector_field;
	/* Contains arbitrary data. */
	uint8_t   echoed_nonce;
	/* pause capability. */
	uint8_t   capability;
	/* Set to 1 to indicate a Remote Fault condition.
	 * Set to 0 to indicate normal operation.*/
	uint8_t   remote_fault;
	/* Should always be set to 0. */
	uint8_t   acknowledge;
	/* Set to 1 to indicate that the device has next pages to send.
	 * Set to 0 to indicate that that device has no next pages to send. */
	uint8_t   next_page;
	/* Must be set to an arbitrary value.
	 * Two devices must have a different nonce for autonegotiation to
	 * operate (a loopback will not allow autonegotiation to complete). */
	uint8_t   transmitted_nonce;
	uint32_t  technology;
#define AL_ETH_AN_TECH_1000BASE_KX  AL_BIT(0)
#define AL_ETH_AN_TECH_10GBASE_KX4  AL_BIT(1)
#define AL_ETH_AN_TECH_10GBASE_KR   AL_BIT(2)
#define AL_ETH_AN_TECH_40GBASE_KR4  AL_BIT(3)
#define AL_ETH_AN_TECH_40GBASE_CR4  AL_BIT(4)
#define AL_ETH_AN_TECH_100GBASE_CR  AL_BIT(5)
	uint8_t   fec_capability;
};

/* AN next page fields */
struct al_eth_an_np {
	/* These bits can be used as message code field or unformatted code field.
	 * When msg_page is true, these bits represent message code field.
	 * Predefined message code field Code Field should be used as specified in the standard
	 * 802.3ap.
	 * For the null message code the value is 0x01.
	 */
	uint16_t	unformatted_code_field;
	/* Flag to keep track of the state of the local device's Toggle bit.
	 * Initial value is taken from base page. Set to 0.
	 */
	al_bool		toggle;
	/* Acknowledge 2 is used to indicate that the receiver is able to act on the information
	 * (or perform the task) defined in the message.
	 */
	al_bool		ack2;
	al_bool		msg_page;
	/* If the device does not have any more Next Pages to send, set to AL_FALSE */
	al_bool		next_page;
	uint16_t	unformatted_code_field1;
	uint16_t	unformatted_code_field2;
};

enum al_eth_kr_cl72_cstate {
	C72_CSTATE_NOT_UPDATED = 0,
	C72_CSTATE_UPDATED = 1,
	C72_CSTATE_MIN = 2,
	C72_CSTATE_MAX = 3,
};

enum al_eth_kr_cl72_coef_op {
	AL_PHY_KR_COEF_UP_HOLD = 0,
	AL_PHY_KR_COEF_UP_INC = 1,
	AL_PHY_KR_COEF_UP_DEC = 2,
	AL_PHY_KR_COEF_UP_RESERVED = 3
};

struct al_eth_kr_coef_up_data {
	enum al_eth_kr_cl72_coef_op c_zero;
	enum al_eth_kr_cl72_coef_op c_plus;
	enum al_eth_kr_cl72_coef_op c_minus;
	al_bool preset;
	al_bool initialize;
};

struct al_eth_kr_status_report_data {
	enum al_eth_kr_cl72_cstate c_zero;
	enum al_eth_kr_cl72_cstate c_plus;
	enum al_eth_kr_cl72_cstate c_minus;
	al_bool receiver_ready;
};

enum al_eth_an_lt_lane {
	AL_ETH_AN__LT_LANE_0,
	AL_ETH_AN__LT_LANE_1,
	AL_ETH_AN__LT_LANE_2,
	AL_ETH_AN__LT_LANE_3,
};

/**
 * get the last received coefficient update message from the link partner
 *
 * @param adapter pointer to the private structure
 * @param lane lane number
 * @param lpcoeff coeff update received
 *
 */
void al_eth_lp_coeff_up_get(
			struct al_hal_eth_adapter *adapter,
			enum al_eth_an_lt_lane lane,
			struct al_eth_kr_coef_up_data *lpcoeff);

/**
 * get the last received status report message from the link partner
 *
 * @param adapter pointer to the private structure
 * @param lane lane number
 * @param status status report received
 *
 */
void al_eth_lp_status_report_get(
			struct al_hal_eth_adapter *adapter,
			enum al_eth_an_lt_lane lane,
			struct al_eth_kr_status_report_data *status);

/**
 * set the coefficient data for the next message that will be sent to lp
 *
 * @param adapter pointer to the private structure
 * @param lane lane number
 * @param ldcoeff coeff update to send
 *
 */
void al_eth_ld_coeff_up_set(
			struct al_hal_eth_adapter *adapter,
			enum al_eth_an_lt_lane lane,
			struct al_eth_kr_coef_up_data *ldcoeff);

/**
 * set the status report message for the next message that will be sent to lp
 *
 * @param adapter pointer to the private structure
 * @param lane lane number
 * @param status status report to send
 *
 */
void al_eth_ld_status_report_set(
			struct al_hal_eth_adapter *adapter,
			enum al_eth_an_lt_lane lane,
			struct al_eth_kr_status_report_data *status);

/**
 * get the receiver frame lock status
 *
 * @param adapter pointer to the private structure
 * @param lane lane number
 *
 * @return true if Training frame delineation is detected, otherwise false.
 */
al_bool al_eth_kr_receiver_frame_lock_get(struct al_hal_eth_adapter *adapter,
					  enum al_eth_an_lt_lane lane);

/**
 * get the start up protocol progress status
 *
 * @param adapter pointer to the private structure
 * @param lane lane number
 *
 * @return true if the startup protocol is in progress.
 */
al_bool al_eth_kr_startup_proto_prog_get(struct al_hal_eth_adapter *adapter,
					 enum al_eth_an_lt_lane lane);

/**
 * indicate the receiver is ready (the link training is completed)
 *
 * @param adapter pointer to the private structure
 * @param lane lane number
 *
 */
void al_eth_receiver_ready_set(struct al_hal_eth_adapter *adapter,
			       enum al_eth_an_lt_lane lane);

/**
 * read Training failure status.
 *
 * @param adapter pointer to the private structure
 * @param lane lane number
 *
 *@return true if Training failure has been detected.
 */
al_bool al_eth_kr_training_status_fail_get(struct al_hal_eth_adapter *adapter,
					   enum al_eth_an_lt_lane lane);

/****************************** auto negotiation *******************************/
/**
 * Initialize Auto-negotiation
 * - Program Ability Registers (Advertisement Registers)
 * - Clear Status latches
 * @param adapter pointer to the private structure
 * @param an_adv pointer to the AN Advertisement Registers structure
 *        when NULL, the registers will not be updated.
 *
 * @return 0 on success. otherwise on failure.
 */
int al_eth_kr_an_init(struct al_hal_eth_adapter *adapter,
		      struct al_eth_an_adv *an_adv);

/**
 * Enable/Restart Auto-negotiation
 *
 * @param adapter pointer to the private structure
 * @param lane lane number
 * @param lt_enable initialize link training as well
 *
 * @return 0 on success. otherwise on failure.
 */
int al_eth_kr_an_start(struct al_hal_eth_adapter *adapter,
		       enum al_eth_an_lt_lane lane,
		       al_bool next_page_enable,
		       al_bool lt_enable);


int al_eth_kr_next_page_write(struct al_hal_eth_adapter *adapter,
			      struct al_eth_an_np *np);

int al_eth_kr_next_page_read(struct al_hal_eth_adapter *adapter,
			     struct al_eth_an_np *np);

/**
 * Stop Auto-negotiation
 *
 * Stopping the auto-negotiation will prevent the mac from sending the last page
 * to the link partner in case it start the AN again. It must be called after
 * link training is completed or the software will lose sync with the HW state
 * machine
 *
 * @param adapter pointer to the private structure
 *
 */
void al_eth_kr_an_stop(struct al_hal_eth_adapter *adapter);

/**
 *  Check Auto-negotiation event done
 *
 * @param adapter pointer to the private structure
 * @param page_received	Set to true if the AN page received indication is set.
 *			Set to false otherwise.
 * @param an_completed	Set to true of the AN completed indication is set.
 *			Set to false otherwise.
 * @param error	Set to true if any error encountered
 *
 */
void al_eth_kr_an_status_check(struct al_hal_eth_adapter *adapter,
			      al_bool *page_received,
			      al_bool *an_completed,
			      al_bool *error);

/**
 *  Read the remote auto-negotiation advertising.
 *  This function is safe to called after al_eth_kr_an_status_check returned
 *  with page_received set.
 *
 * @param adapter pointer to the private structure
 * @param an_adv pointer to the AN Advertisement Registers structure
 *
 */
void al_eth_kr_an_read_adv(struct al_hal_eth_adapter *adapter,
			   struct al_eth_an_adv *an_adv);

/****************************** link training **********************************/
/**
 *  Initialize Link-training.
 *  Clear the status register and set the local coefficient update and status
 *  to zero.
 *
 * @param adapter pointer to the private structure
 * @param lane lane number
 *
 */
void al_eth_kr_lt_initialize(struct al_hal_eth_adapter *adapter,
			     enum al_eth_an_lt_lane lane);

/**
 * Wait for frame lock.
 *
 * @param adapter pointer to the private structure
 * @param lane lane number
 * @param timeout timeout in usec.
 *
 * @return true if frame lock received. false otherwise.
 */
al_bool al_eth_kr_lt_frame_lock_wait(struct al_hal_eth_adapter *adapter,
				     enum al_eth_an_lt_lane lane,
				     uint32_t timeout);

/**
 * reset the 10GBase- KR startup protocol and begin its operation
 *
 * @param adapter pointer to the private structure
 * @param lane lane number
 *
 */
void al_eth_kr_lt_restart(struct al_hal_eth_adapter *adapter,
			  enum al_eth_an_lt_lane lane);

/**
 * reset the 10GBase- KR startup protocol and end its operation
 *
 * @param adapter pointer to the private structure
 * @param lane lane number
 *
 */
void al_eth_kr_lt_stop(struct al_hal_eth_adapter *adapter,
		       enum al_eth_an_lt_lane lane);

#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */
#endif /*__AL_HAL_ETH_KR_H__*/
/** @} end of Ethernet kr group */
