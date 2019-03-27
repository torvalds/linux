/*-
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef _SCIC_SDS_PHY_H_
#define _SCIC_SDS_PHY_H_

/**
 * @file
 *
 * @brief This file contains the structures, constants and prototypes for the
 *        SCIC_SDS_PHY object.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/intel_sata.h>
#include <dev/isci/scil/intel_sas.h>
#include <dev/isci/scil/sci_base_phy.h>
#include <dev/isci/scil/scu_registers.h>
#include <dev/isci/scil/scu_event_codes.h>

/**
 * This is the timeout value for the SATA phy to wait for a SIGNATURE FIS
 * before restarting the starting state machine.  Technically, the old
 * parallel ATA specification required up to 30 seconds for a device to
 * issue its signature FIS as a result of a soft reset.  Now we see that
 * devices respond generally within 15 seconds, but we'll use 25 for now.
 */
#define SCIC_SDS_SIGNATURE_FIS_TIMEOUT    25000

/**
 * This is the timeout for the SATA OOB/SN because the hardware does not
 * recognize a hot plug after OOB signal but before the SN signals.  We
 * need to make sure after a hotplug timeout if we have not received the
 * speed event notification from the hardware that we restart the hardware
 * OOB state machine.
 */
#define SCIC_SDS_SATA_LINK_TRAINING_TIMEOUT  250

/**
 * @enum SCIC_SDS_PHY_STARTING_SUBSTATES
 */
enum SCIC_SDS_PHY_STARTING_SUBSTATES
{
   /**
    * Initial state
    */
   SCIC_SDS_PHY_STARTING_SUBSTATE_INITIAL,

   /**
    * Wait state for the hardware OSSP event type notification
    */
   SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_OSSP_EN,

   /**
    * Wait state for the PHY speed notification
    */
   SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SAS_SPEED_EN,

   /**
    * Wait state for the IAF Unsolicited frame notification
    */
   SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_IAF_UF,

   /**
    * Wait state for the request to consume power
    */
   SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SAS_POWER,

   /**
    * Wait state for request to consume power
    */
   SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_POWER,

   /**
    * Wait state for the SATA PHY notification
    */
   SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_PHY_EN,

   /**
    * Wait for the SATA PHY speed notification
    */
   SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_SPEED_EN,

   /**
    * Wait state for the SIGNATURE FIS unsolicited frame notification
    */
   SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SIG_FIS_UF,

   /**
    * Exit state for this state machine
    */
   SCIC_SDS_PHY_STARTING_SUBSTATE_FINAL,

   /**
    * Maximum number of substates for the STARTING state machine
    */
   SCIC_SDS_PHY_STARTING_MAX_SUBSTATES
};

struct SCIC_SDS_PORT;
struct SCIC_SDS_CONTROLLER;

#ifdef SCIC_DEBUG_ENABLED
#define MAX_STATE_TRANSITION_RECORD    (256)

/**
 * Debug code to record the state transitions for the phy object
 */
typedef struct SCIC_SDS_PHY_STATE_RECORD
{
   SCI_BASE_OBSERVER_T  base_state_observer;
   SCI_BASE_OBSERVER_T  starting_state_observer;

   U16 index;

   U32 state_transition_table[MAX_STATE_TRANSITION_RECORD];

} SCIC_SDS_PHY_STATE_RECORD_T;
#endif // SCIC_DEBUG_ENABLED

/**
 * @enum
 *
 * @brief This enumeration provides a named phy type for the state machine
 */
enum SCIC_SDS_PHY_PROTOCOL
{
   /**
    * This is an unknown phy type since there is either nothing on the other
    * end or we have not detected the phy type as yet.
    */
   SCIC_SDS_PHY_PROTOCOL_UNKNOWN,

   /**
    * This is a SAS PHY
    */
   SCIC_SDS_PHY_PROTOCOL_SAS,

   /**
    * This is a SATA PHY
    */
   SCIC_SDS_PHY_PROTOCOL_SATA,

   SCIC_SDS_MAX_PHY_PROTOCOLS
};

/**
 * @struct SCIC_SDS_PHY
 *
 * @brief This structure  contains or references all of the data necessary to
 *        represent the core phy object and SCU hardware protocol engine.
 */
typedef struct SCIC_SDS_PHY
{
   SCI_BASE_PHY_T parent;

   /**
    * This field specifies the port object that owns/contains this phy.
    */
   struct SCIC_SDS_PORT * owning_port;

   /**
    * This field indicates whether the phy supports 1.5 Gb/s, 3.0 Gb/s,
    * or 6.0 Gb/s operation.
    */
   SCI_SAS_LINK_RATE max_negotiated_speed;

   /**
    * This member specifies the protocol being utilized on this phy.  This
    * field contains a legitamite value once the PHY has link trained with
    * a remote phy.
    */
   enum SCIC_SDS_PHY_PROTOCOL protocol;

   /**
    * This field specifies the index with which this phy is associated (0-3).
    */
   U8 phy_index;

   /**
    * This member indicates if this particular PHY has received a BCN while
    * it had no port assignement.  This BCN will be reported once the phy is
    * assigned to a port.
    */
   BOOL bcn_received_while_port_unassigned;

   /**
    * This field indicates if this PHY is currently in the process of
    * link training (i.e. it has started OOB, but has yet to perform
    * IAF exchange/Signature FIS reception).
    */
   BOOL is_in_link_training;

   union
   {
      struct
      {
         SCI_SAS_IDENTIFY_ADDRESS_FRAME_T identify_address_frame_buffer;

      } sas;

      struct
      {
         SATA_FIS_REG_D2H_T signature_fis_buffer;

      } sata;

   } phy_type;

   /**
    * This field contains a reference to the timer utilized in detecting
    * when a signature FIS timeout has occurred.  The signature FIS is the
    * first FIS sent by an attached SATA device after OOB/SN.
    */
   void * sata_timeout_timer;

   struct SCIC_SDS_PHY_STATE_HANDLER *state_handlers;

   SCI_BASE_STATE_MACHINE_T starting_substate_machine;

   #ifdef SCI_LOGGING
   SCI_BASE_STATE_MACHINE_LOGGER_T starting_substate_machine_logger;
   #endif

   #ifdef SCIC_DEBUG_ENABLED
   SCIC_SDS_PHY_STATE_RECORD_T state_record;
   #endif // SCIC_DEBUG_ENABLED

   /**
    * This field tracks how many errors of each type have been detected since
    *  the last controller reset or counter clear.  Note that these are only
    *  for the error types that our driver needs to count manually.  See
    *  SCU_ERR_CNT_* values defined in scu_event_codes.h.
    */
   U32   error_counter[SCU_ERR_CNT_MAX_INDEX];

   /**
    * This field is the pointer to the transport layer register for the SCU
    * hardware.
    */
   SCU_TRANSPORT_LAYER_REGISTERS_T *transport_layer_registers;

   /**
    * This field points to the link layer register set within the SCU.
    */
   SCU_LINK_LAYER_REGISTERS_T *link_layer_registers;

} SCIC_SDS_PHY_T;


typedef SCI_STATUS (*SCIC_SDS_PHY_EVENT_HANDLER_T)(SCIC_SDS_PHY_T *, U32);
typedef SCI_STATUS (*SCIC_SDS_PHY_FRAME_HANDLER_T)(SCIC_SDS_PHY_T *, U32);
typedef SCI_STATUS (*SCIC_SDS_PHY_POWER_HANDLER_T)(SCIC_SDS_PHY_T *);

/**
 * @struct SCIC_SDS_PHY_STATE_HANDLER
 */
typedef struct SCIC_SDS_PHY_STATE_HANDLER
{
   /**
    * This is the SCI_BASE_PHY object state handlers.
    */
   SCI_BASE_PHY_STATE_HANDLER_T parent;

   /**
    * The state handler for unsolicited frames received from the SCU hardware.
    */
   SCIC_SDS_PHY_FRAME_HANDLER_T frame_handler;

   /**
    * The state handler for events received from the SCU hardware.
    */
   SCIC_SDS_PHY_EVENT_HANDLER_T event_handler;

   /**
    * The state handler for staggered spinup.
    */
   SCIC_SDS_PHY_POWER_HANDLER_T consume_power_handler;

} SCIC_SDS_PHY_STATE_HANDLER_T;

extern SCIC_SDS_PHY_STATE_HANDLER_T scic_sds_phy_state_handler_table[];
extern SCI_BASE_STATE_T scic_sds_phy_state_table[];
extern SCI_BASE_STATE_T scic_sds_phy_starting_substates[];
extern SCIC_SDS_PHY_STATE_HANDLER_T
       scic_sds_phy_starting_substate_handler_table[];


/**
 * This macro returns the phy index for the specified phy
 */
#define scic_sds_phy_get_index(phy) \
   ((phy)->phy_index)

/**
 * @brief This macro returns the controller for this phy
 */
#define scic_sds_phy_get_controller(phy) \
   (scic_sds_port_get_controller((phy)->owning_port))

/**
 * @brief This macro returns the state machine for the base phy
 */
#define scic_sds_phy_get_base_state_machine(phy) \
   (&(phy)->parent.state_machine)

/**
 * @brief This macro returns the starting substate machine for
 *        this phy
 */
#define scic_sds_phy_get_starting_substate_machine(phy) \
   (&(phy)->starting_substate_machine)

/**
 * @brief This macro sets the state handlers for this phy object
 */
#define scic_sds_phy_set_state_handlers(phy, handlers) \
   ((phy)->state_handlers = (handlers))

/**
 * This macro set the base state handlers for the phy object.
 */
#define scic_sds_phy_set_base_state_handlers(phy, state_id) \
   scic_sds_phy_set_state_handlers( \
      (phy), \
      &scic_sds_phy_state_handler_table[(state_id)] \
   )

/**
 * This macro returns TRUE if the current base state for this phy is
 * SCI_BASE_PHY_STATE_READY
 */
#define scic_sds_phy_is_ready(phy) \
   ( \
         SCI_BASE_PHY_STATE_READY \
      == sci_base_state_machine_get_state( \
            scic_sds_phy_get_base_state_machine(phy) \
         ) \
   )

// ---------------------------------------------------------------------------

U32 scic_sds_phy_get_object_size(void);

U32 scic_sds_phy_get_min_timer_count(void);

U32 scic_sds_phy_get_max_timer_count(void);

// ---------------------------------------------------------------------------

void scic_sds_phy_construct(
   struct SCIC_SDS_PHY  *this_phy,
   struct SCIC_SDS_PORT *owning_port,
   U8                    phy_index
);

SCI_PORT_HANDLE_T scic_sds_phy_get_port(
   SCIC_SDS_PHY_T *this_phy
);

void scic_sds_phy_set_port(
   struct SCIC_SDS_PHY  *this_phy,
   struct SCIC_SDS_PORT *owning_port
);

SCI_STATUS scic_sds_phy_initialize(
   SCIC_SDS_PHY_T             *this_phy,
   void                       *transport_layer_registers,
   SCU_LINK_LAYER_REGISTERS_T *link_layer_registers
);

SCI_STATUS scic_sds_phy_reset(
   SCIC_SDS_PHY_T * this_phy
);

void scic_sds_phy_sata_timeout(
   SCI_OBJECT_HANDLE_T cookie
);

// ---------------------------------------------------------------------------

void scic_sds_phy_suspend(
   struct SCIC_SDS_PHY  *this_phy
);

void scic_sds_phy_resume(
   struct SCIC_SDS_PHY  *this_phy
);

void scic_sds_phy_setup_transport(
   struct SCIC_SDS_PHY * this_phy,
   U32                   device_id
);

// ---------------------------------------------------------------------------

SCI_STATUS scic_sds_phy_event_handler(
   SCIC_SDS_PHY_T *this_phy,
   U32 event_code
);

SCI_STATUS scic_sds_phy_frame_handler(
   SCIC_SDS_PHY_T *this_phy,
   U32 frame_index
);

SCI_STATUS scic_sds_phy_consume_power_handler(
   SCIC_SDS_PHY_T *this_phy
);

void scic_sds_phy_get_sas_address(
   SCIC_SDS_PHY_T    *this_phy,
   SCI_SAS_ADDRESS_T *sas_address
);

void scic_sds_phy_get_attached_sas_address(
   SCIC_SDS_PHY_T    *this_phy,
   SCI_SAS_ADDRESS_T *sas_address
);

void scic_sds_phy_get_protocols(
   SCIC_SDS_PHY_T *this_phy,
   SCI_SAS_IDENTIFY_ADDRESS_FRAME_PROTOCOLS_T * protocols
);

void scic_sds_phy_get_attached_phy_protocols(
   SCIC_SDS_PHY_T *this_phy,
   SCI_SAS_IDENTIFY_ADDRESS_FRAME_PROTOCOLS_T * protocols
);

//****************************************************************************-
//* SCIC SDS PHY Handler Methods
//****************************************************************************-

SCI_STATUS scic_sds_phy_default_start_handler(
   SCI_BASE_PHY_T *phy
);

SCI_STATUS scic_sds_phy_default_stop_handler(
   SCI_BASE_PHY_T *phy
);

SCI_STATUS scic_sds_phy_default_reset_handler(
   SCI_BASE_PHY_T * phy
);

SCI_STATUS scic_sds_phy_default_destroy_handler(
   SCI_BASE_PHY_T *phy
);

SCI_STATUS scic_sds_phy_default_frame_handler(
   SCIC_SDS_PHY_T *phy,
   U32 frame_index
);

SCI_STATUS scic_sds_phy_default_event_handler(
   SCIC_SDS_PHY_T *phy,
   U32 evnet_code
);

SCI_STATUS scic_sds_phy_default_consume_power_handler(
   SCIC_SDS_PHY_T *phy
);

void scic_sds_phy_release_resource(
   struct SCIC_SDS_CONTROLLER * controller,
   struct SCIC_SDS_PHY        * phy
);

void scic_sds_phy_restart_starting_state(
   struct SCIC_SDS_PHY        * this_phy
);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIC_SDS_PHY_H_
