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
#ifndef _SCIC_SDS_REMOTE_DEVICE_H_
#define _SCIC_SDS_REMOTE_DEVICE_H_

/**
 * @file
 *
 * @brief This file contains the structures, constants, and prototypes for the
 *        SCIC_SDS_REMOTE_DEVICE object.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/intel_sas.h>
#include <dev/isci/scil/sci_base_remote_device.h>
#include <dev/isci/scil/sci_base_request.h>
#include <dev/isci/scil/sci_base_state_machine_logger.h>
#include <dev/isci/scil/scu_remote_node_context.h>
#include <dev/isci/scil/scic_sds_remote_node_context.h>

struct SCIC_SDS_CONTROLLER;
struct SCIC_SDS_PORT;
struct SCIC_SDS_REQUEST;
struct SCIC_SDS_REMOTE_DEVICE_STATE_HANDLER;

/**
 * @enum SCIC_SDS_SSP_REMOTE_DEVICE_READY_SUBSTATES
 *
 * This is the enumeration of the ready substates for the
 * SCIC_SDS_REMOTE_DEVICE.
 */
enum SCIC_SDS_SSP_REMOTE_DEVICE_READY_SUBSTATES
{
   /**
    * This is the initial state for the remote device ready substate.
    */
   SCIC_SDS_SSP_REMOTE_DEVICE_READY_SUBSTATE_INITIAL,

   /**
    * This is the ready operational substate for the remote device.  This is the
    * normal operational state for a remote device.
    */
   SCIC_SDS_SSP_REMOTE_DEVICE_READY_SUBSTATE_OPERATIONAL,

   /**
    * This is the suspended state for the remote device.  This is the state that
    * the device is placed in when a RNC suspend is received by the SCU hardware.
    */
   SCIC_SDS_SSP_REMOTE_DEVICE_READY_SUBSTATE_SUSPENDED,

   /**
    * This is the final state that the device is placed in before a change to the
    * base state machine.
    */
   SCIC_SDS_SSP_REMOTE_DEVICE_READY_SUBSTATE_FINAL,

   SCIC_SDS_SSP_REMOTE_DEVICE_READY_MAX_SUBSTATES
};

/**
 * @enum SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATES
 *
 * This is the enumeration for the SCIC_SDS_REMOTE_DEVICE ready substates for
 * the STP remote device.
 */
enum SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATES
{
   /**
    * This is the idle substate for the stp remote device.  When there are no
    * active IO for the device it is in this state.
    */
   SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_IDLE,

   /**
    * This is the command state for for the STP remote device.  This state is
    * entered when the device is processing a non-NCQ command.  The device object
    * will fail any new start IO requests until this command is complete.
    */
   SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_CMD,

   /**
    * This is the NCQ state for the STP remote device.  This state is entered
    * when the device is processing an NCQ reuqest.  It will remain in this state
    * so long as there is one or more NCQ requests being processed.
    */
   SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_NCQ,

   /**
    * This is the NCQ error state for the STP remote device.  This state is
    * entered when an SDB error FIS is received by the device object while in the
    * NCQ state.  The device object will only accept a READ LOG command while in
    * this state.
    */
   SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_NCQ_ERROR,

#if !defined(DISABLE_ATAPI)
   /**
    * This is the ATAPI error state for the STP ATAPI remote device.  This state is
    * entered when ATAPI device sends error status FIS without data while the device
    * object is in CMD state. A suspension event is expected in this state. The device
    * object will resume right away.
    */
   SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_ATAPI_ERROR,
#endif

   /**
    * This is the READY substate indicates the device is waiting for the RESET task
    * coming to be recovered from certain hardware specific error.
    */
   SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_AWAIT_RESET,

   SCIC_SDS_STP_REMOTE_DEVICE_READY_MAX_SUBSTATES
};


/**
 * @enum SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATES
 *
 * This is the enumeration of the ready substates for the SMP REMOTE DEVICE.
 */

enum SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATES
{
   /**
    * This is the ready operational substate for the remote device.  This is the
    * normal operational state for a remote device.
    */
   SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_IDLE,

   /**
    * This is the suspended state for the remote device.  This is the state that
    * the device is placed in when a RNC suspend is received by the SCU hardware.
    */
   SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_CMD,

   SCIC_SDS_SMP_REMOTE_DEVICE_READY_MAX_SUBSTATES
};




/**
 * @struct SCIC_SDS_REMOTE_DEVICE
 *
 * @brief  This structure contains the data for an SCU implementation of
 *         the SCU Core device data.
 */
typedef struct SCIC_SDS_REMOTE_DEVICE
{
   /**
    * This field is the common base for all remote device objects.
    */
   SCI_BASE_REMOTE_DEVICE_T parent;

   /**
    * This field is the programmed device port width.  This value is written to
    * the RCN data structure to tell the SCU how many open connections this
    * device can have.
    */
   U32 device_port_width;

   /**
    * This field is the programmed connection rate for this remote device.  It is
    * used to program the TC with the maximum allowed connection rate.
    */
   SCI_SAS_LINK_RATE connection_rate;

   /**
    * This field contains the allowed target protocols for this remote device.
    */
   SMP_DISCOVER_RESPONSE_PROTOCOLS_T target_protocols;

   /**
    * This field contains the device SAS address.
    */
   SCI_SAS_ADDRESS_T device_address;

   /**
    * This filed is assinged the value of TRUE if the device is directly attached
    * to the port.
    */
   BOOL is_direct_attached;

#if !defined(DISABLE_ATAPI)
   /**
    * This filed is assinged the value of TRUE if the device is an ATAPI device.
    */
   BOOL is_atapi;
#endif

   /**
    * This filed contains a pointer back to the port to which this device is
    * assigned.
    */
   struct SCIC_SDS_PORT *owning_port;

   /**
    * This field contains the SCU silicon remote node context specific
    * information.
    */
   struct SCIC_SDS_REMOTE_NODE_CONTEXT * rnc;

   /**
    * This field contains the stated request count for the remote device.  The
    * device can not reach the SCI_BASE_REMOTE_DEVICE_STATE_STOPPED until all
    * requests are complete and the rnc_posted value is FALSE.
    */
   U32 started_request_count;

   /**
    * This field contains a pointer to the working request object.  It is only
    * used only for SATA requests since the unsolicited frames we get from the
    * hardware have no Tag value to look up the io request object.
    */
   struct SCIC_SDS_REQUEST * working_request;

   /**
    * This field contains the reason for the remote device going not_ready.  It is
    * assigned in the state handlers and used in the state transition.
    */
   U32 not_ready_reason;

   /**
    * This field is TRUE if this remote device has an initialzied ready substate
    * machine. SSP devices do not have a ready substate machine and STP devices
    * have a ready substate machine.
    */
   BOOL has_ready_substate_machine;

   /**
    * This field contains the state machine for the ready substate machine for
    * this SCIC_SDS_REMOTE_DEVICE object.
    */
   SCI_BASE_STATE_MACHINE_T ready_substate_machine;

   /**
    * This field maintains the set of state handlers for the remote device
    * object.  These are changed each time the remote device enters a new state.
    */
   struct SCIC_SDS_REMOTE_DEVICE_STATE_HANDLER *state_handlers;

   #ifdef SCI_LOGGING
   /**
    * This field conatins the ready substate machine logger.  The logger will
    * emit a message each time the ready substate machine changes state.
    */
   SCI_BASE_STATE_MACHINE_LOGGER_T ready_substate_machine_logger;
   #endif

} SCIC_SDS_REMOTE_DEVICE_T;


typedef SCI_STATUS (*SCIC_SDS_REMOTE_DEVICE_HANDLER_T)(
                                 SCIC_SDS_REMOTE_DEVICE_T *this_device);

typedef SCI_STATUS (*SCIC_SDS_REMOTE_DEVICE_SUSPEND_HANDLER_T)(
                                 SCIC_SDS_REMOTE_DEVICE_T *this_device,
                                 U32                       suspend_type);

typedef SCI_STATUS (*SCIC_SDS_REMOTE_DEVICE_RESUME_HANDLER_T)(
                                 SCIC_SDS_REMOTE_DEVICE_T *this_device);

typedef SCI_STATUS (*SCIC_SDS_REMOTE_DEVICE_FRAME_HANDLER_T)(
                                  SCIC_SDS_REMOTE_DEVICE_T *this_device,
                                  U32                       frame_index);

typedef SCI_STATUS (*SCIC_SDS_REMOTE_DEVICE_EVENT_HANDLER_T)(
                                  SCIC_SDS_REMOTE_DEVICE_T *this_device,
                                  U32                       event_code);

typedef void (*SCIC_SDS_REMOTE_DEVICE_READY_NOT_READY_HANDLER_T)(
                                  SCIC_SDS_REMOTE_DEVICE_T *this_device);

/**
 * @struct SCIC_SDS_REMOTE_DEVICE_STATE_HANDLER
 * @brief This structure conains the state handlers that are needed to
 *        process requests for the SCU remote device objects.
 */
typedef struct SCIC_SDS_REMOTE_DEVICE_STATE_HANDLER
{
   SCI_BASE_REMOTE_DEVICE_STATE_HANDLER_T parent;

   SCIC_SDS_REMOTE_DEVICE_SUSPEND_HANDLER_T suspend_handler;
   SCIC_SDS_REMOTE_DEVICE_RESUME_HANDLER_T  resume_handler;

   SCIC_SDS_REMOTE_DEVICE_EVENT_HANDLER_T event_handler;
   SCIC_SDS_REMOTE_DEVICE_FRAME_HANDLER_T frame_handler;

} SCIC_SDS_REMOTE_DEVICE_STATE_HANDLER_T;


extern SCI_BASE_STATE_T scic_sds_remote_device_state_table[];
extern SCI_BASE_STATE_T scic_sds_ssp_remote_device_ready_substate_table[];
extern SCI_BASE_STATE_T scic_sds_stp_remote_device_ready_substate_table[];
extern SCI_BASE_STATE_T scic_sds_smp_remote_device_ready_substate_table[];

extern SCIC_SDS_REMOTE_DEVICE_STATE_HANDLER_T
               scic_sds_remote_device_state_handler_table[];
extern SCIC_SDS_REMOTE_DEVICE_STATE_HANDLER_T
               scic_sds_ssp_remote_device_ready_substate_handler_table[];
extern SCIC_SDS_REMOTE_DEVICE_STATE_HANDLER_T
               scic_sds_stp_remote_device_ready_substate_handler_table[];
extern SCIC_SDS_REMOTE_DEVICE_STATE_HANDLER_T
               scic_sds_smp_remote_device_ready_substate_handler_table[];

/**
 * This macro incrments the request count for this device
 */
#define scic_sds_remote_device_increment_request_count(this_device) \
   ((this_device)->started_request_count++)

/**
 * This macro decrements the request count for this device.  This count
 * will never decrment past 0.
 */
#define scic_sds_remote_device_decrement_request_count(this_device) \
   ((this_device)->started_request_count > 0 ? \
      (this_device)->started_request_count-- : 0)

/**
 * This is a helper macro to return the current device request count.
 */
#define scic_sds_remote_device_get_request_count(this_device) \
   ((this_device)->started_request_count)

/**
 * This macro returns the owning port of this remote device obejct.
 */
#define scic_sds_remote_device_get_port(this_device) \
   ((this_device)->owning_port)

/**
 * This macro returns the controller object that contains this device
 * object
 */
#define scic_sds_remote_device_get_controller(this_device) \
   scic_sds_port_get_controller(scic_sds_remote_device_get_port(this_device))

/**
 * This macro sets the remote device state handlers pointer and is set on
 * entry to each device state.
 */
#define scic_sds_remote_device_set_state_handlers(this_device, handlers) \
   ((this_device)->state_handlers = (handlers))

/**
 * This macro returns the base sate machine object for the remote device.
 */
#define scic_sds_remote_device_get_base_state_machine(this_device) \
   (&(this_device)->parent.state_machine)

/**
 * This macro returns the remote device ready substate machine
 */
#define scic_sds_remote_device_get_ready_substate_machine(this_device) \
   (&(this_device)->ready_substate_machine)

/**
 * This macro returns the owning port of this device
 */
#define scic_sds_remote_device_get_port(this_device) \
   ((this_device)->owning_port)

/**
 * This macro returns the remote device sequence value
 */
#define scic_sds_remote_device_get_sequence(this_device) \
   ( \
      scic_sds_remote_device_get_controller(this_device)->\
         remote_device_sequence[(this_device)->rnc->remote_node_index] \
   )

/**
 * This macro returns the controllers protocol engine group
 */
#define scic_sds_remote_device_get_controller_peg(this_device) \
   ( \
      scic_sds_controller_get_protocol_engine_group( \
         scic_sds_port_get_controller( \
            scic_sds_remote_device_get_port(this_device) \
         ) \
      ) \
   )

/**
 * This macro returns the port index for the devices owning port
 */
#define scic_sds_remote_device_get_port_index(this_device) \
   (scic_sds_port_get_index(scic_sds_remote_device_get_port(this_device)))

/**
 * This macro returns the remote node index for this device object
 */
#define scic_sds_remote_device_get_index(this_device) \
   ((this_device)->rnc->remote_node_index)

/**
 * This macro builds a remote device context for the SCU post request
 * operation
 */
#define scic_sds_remote_device_build_command_context(device, command) \
   (   (command) \
     | ((U32)(scic_sds_remote_device_get_controller_peg((device))) << SCU_CONTEXT_COMMAND_PROTOCOL_ENGINE_GROUP_SHIFT)\
     | ((U32)(scic_sds_remote_device_get_port_index((device))) << SCU_CONTEXT_COMMAND_LOGICAL_PORT_SHIFT) \
     | (scic_sds_remote_device_get_index((device))) \
   )

/**
 * This macro makes the working request assingment for the remote device
 * object. To clear the working request use this macro with a NULL request
 * object.
 */
#define scic_sds_remote_device_set_working_request(device, request) \
   ((device)->working_request = (request))

// ---------------------------------------------------------------------------

U32 scic_sds_remote_device_get_min_timer_count(void);

U32 scic_sds_remote_device_get_max_timer_count(void);

SCI_STATUS scic_sds_remote_device_frame_handler(
   SCIC_SDS_REMOTE_DEVICE_T *this_device,
   U32                       frame_index
);

SCI_STATUS scic_sds_remote_device_event_handler(
   SCIC_SDS_REMOTE_DEVICE_T *this_device,
   U32                       event_code
);

SCI_STATUS scic_sds_remote_device_start_io(
   struct SCIC_SDS_CONTROLLER *controller,
   SCIC_SDS_REMOTE_DEVICE_T   *this_device,
   struct SCIC_SDS_REQUEST    *io_request
);

SCI_STATUS scic_sds_remote_device_complete_io(
   struct SCIC_SDS_CONTROLLER *controller,
   SCIC_SDS_REMOTE_DEVICE_T   *this_device,
   struct SCIC_SDS_REQUEST    *io_request
);

SCI_STATUS scic_sds_remote_device_resume(
   SCIC_SDS_REMOTE_DEVICE_T *this_device
);

SCI_STATUS scic_sds_remote_device_suspend(
   SCIC_SDS_REMOTE_DEVICE_T *this_device,
   U32                       suspend_type
);

SCI_STATUS scic_sds_remote_device_start_task(
   struct SCIC_SDS_CONTROLLER  *controller,
   SCIC_SDS_REMOTE_DEVICE_T    *this_device,
   struct SCIC_SDS_REQUEST     *io_request
);

void scic_sds_remote_device_post_request(
   SCIC_SDS_REMOTE_DEVICE_T * this_device,
   U32                        request
);

#if !defined(DISABLE_ATAPI)
BOOL scic_sds_remote_device_is_atapi(
   SCIC_SDS_REMOTE_DEVICE_T    *this_device
);
#else // !defined(DISABLE_ATAPI)
#define scic_sds_remote_device_is_atapi(this_device) FALSE
#endif // !defined(DISABLE_ATAPI)

// ---------------------------------------------------------------------------

#ifdef SCI_LOGGING
void scic_sds_remote_device_initialize_state_logging(
   SCIC_SDS_REMOTE_DEVICE_T *this_device
);

void scic_sds_remote_device_deinitialize_state_logging(
   SCIC_SDS_REMOTE_DEVICE_T *this_device
);
#else // SCI_LOGGING
#define scic_sds_remote_device_initialize_state_logging(x)
#define scic_sds_remote_device_deinitialize_state_logging(x)
#endif // SCI_LOGGING

// ---------------------------------------------------------------------------

void scic_sds_remote_device_start_request(
   SCIC_SDS_REMOTE_DEVICE_T * this_device,
   struct SCIC_SDS_REQUEST  * the_request,
   SCI_STATUS                 status
);

void scic_sds_remote_device_continue_request(
   SCIC_SDS_REMOTE_DEVICE_T * this_device
);

SCI_STATUS scic_sds_remote_device_default_start_handler(
   SCI_BASE_REMOTE_DEVICE_T *this_device
);

SCI_STATUS scic_sds_remote_device_default_stop_handler(
   SCI_BASE_REMOTE_DEVICE_T *this_device
);

SCI_STATUS scic_sds_remote_device_default_fail_handler(
   SCI_BASE_REMOTE_DEVICE_T *this_device
);

SCI_STATUS scic_sds_remote_device_default_destruct_handler(
   SCI_BASE_REMOTE_DEVICE_T *this_device
);

SCI_STATUS scic_sds_remote_device_default_reset_handler(
   SCI_BASE_REMOTE_DEVICE_T *device
);

SCI_STATUS scic_sds_remote_device_default_reset_complete_handler(
   SCI_BASE_REMOTE_DEVICE_T *device
);

SCI_STATUS scic_sds_remote_device_default_start_request_handler(
   SCI_BASE_REMOTE_DEVICE_T *device,
   SCI_BASE_REQUEST_T       *request
);

SCI_STATUS scic_sds_remote_device_default_complete_request_handler(
   SCI_BASE_REMOTE_DEVICE_T *device,
   SCI_BASE_REQUEST_T       *request
);

SCI_STATUS scic_sds_remote_device_default_continue_request_handler(
   SCI_BASE_REMOTE_DEVICE_T *device,
   SCI_BASE_REQUEST_T       *request
);

SCI_STATUS scic_sds_remote_device_default_suspend_handler(
   SCIC_SDS_REMOTE_DEVICE_T *this_device,
   U32                       suspend_type
);

SCI_STATUS scic_sds_remote_device_default_resume_handler(
   SCIC_SDS_REMOTE_DEVICE_T *this_device
);

SCI_STATUS  scic_sds_remote_device_default_event_handler(
   SCIC_SDS_REMOTE_DEVICE_T *this_device,
   U32                       event_code
);

SCI_STATUS scic_sds_remote_device_default_frame_handler(
   SCIC_SDS_REMOTE_DEVICE_T *this_device,
   U32                       frame_index
);

// ---------------------------------------------------------------------------

SCI_STATUS scic_sds_remote_device_ready_state_stop_handler(
   SCI_BASE_REMOTE_DEVICE_T *device
);

SCI_STATUS scic_sds_remote_device_ready_state_reset_handler(
   SCI_BASE_REMOTE_DEVICE_T *device
);

SCI_STATUS scic_sds_remote_device_general_frame_handler(
   SCIC_SDS_REMOTE_DEVICE_T *this_device,
   U32                       frame_index
);

SCI_STATUS scic_sds_remote_device_general_event_handler(
   SCIC_SDS_REMOTE_DEVICE_T *this_device,
   U32                       event_code
);

SCI_STATUS scic_sds_ssp_remote_device_ready_suspended_substate_resume_handler(
   SCIC_SDS_REMOTE_DEVICE_T *this_device
);

// ---------------------------------------------------------------------------

void scic_sds_remote_device_get_info_from_smp_discover_response(
   SCIC_SDS_REMOTE_DEVICE_T    * this_device,
   SMP_RESPONSE_DISCOVER_T     * discover_response
);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIC_SDS_REMOTE_DEVICE_H_
