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
#ifndef _SCIF_SAS_REMOTE_DEVICE_H_
#define _SCIF_SAS_REMOTE_DEVICE_H_

/**
 * @file
 *
 * @brief This file contains the protected interface structures, constants,
 *        and methods for the SCIF_SAS_REMOTE_DEVICE object.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/scif_remote_device.h>

#include <dev/isci/scil/sci_base_remote_device.h>
#include <dev/isci/scil/sci_base_request.h>
#include <dev/isci/scil/sci_base_state_machine_logger.h>
#include <dev/isci/scil/scif_sas_stp_remote_device.h>
#include <dev/isci/scil/scif_sas_smp_remote_device.h>


struct SCIF_SAS_DOMAIN;
struct SCIF_SAS_REMOTE_DEVICE;
struct SCIF_SAS_REQUEST;

/**
 * This constant indicates the number of milliseconds to wait for the core
 * to start/stop it's remote device object.
 */
#define SCIF_SAS_REMOTE_DEVICE_CORE_OP_TIMEOUT 1000

/**
 * @enum _SCIF_SAS_REMOTE_DEVICE_STARTING_SUBSTATES
 *
 * @brief This enumeration depicts all the substates for the remote device's
 *        starting substate machine.
 */
typedef enum _SCIF_SAS_REMOTE_DEVICE_STARTING_SUBSTATES
{
   /**
    * This state indicates that the framework is waiting for the core to
    * issue a scic_cb_remote_device_start_complete() notification.
    */
   SCIF_SAS_REMOTE_DEVICE_STARTING_SUBSTATE_AWAIT_COMPLETE,

   /**
    * This state indicates that the core has received the core's
    * scic_cb_remote_device_start_complete() notification.
    */
   SCIF_SAS_REMOTE_DEVICE_STARTING_SUBSTATE_AWAIT_READY,

   SCIF_SAS_REMOTE_DEVICE_STARTING_SUBSTATE_MAX_STATES

} SCIF_SAS_REMOTE_DEVICE_STARTING_SUBSTATES;

/**
 * @enum _SCIF_SAS_REMOTE_DEVICE_READY_SUBSTATES
 *
 * @brief This enumeration depicts all of the substates for the remote
 *        device READY substate machine.
 */
typedef enum _SCIF_SAS_REMOTE_DEVICE_READY_SUBSTATES
{
   /**
    * The Operational sub-state indicates that the remote device object
    * is capable of receiving and handling all request types.
    */
   SCIF_SAS_REMOTE_DEVICE_READY_SUBSTATE_OPERATIONAL,

   /**
    * This substate indicates that core remote device is not ready.
    * As a result, no new IO or Task Management requests are allowed.
    */
   SCIF_SAS_REMOTE_DEVICE_READY_SUBSTATE_SUSPENDED,

   /**
    * This substate indicates that task management to this device is
    * ongoing and new IO requests are not allowed.
    */
   SCIF_SAS_REMOTE_DEVICE_READY_SUBSTATE_TASK_MGMT,

   /**
   * This substate indicates that core remote device is not ready due
   *  to an NCQ error.  As a result, no new IO requests are allowed.
   */
   SCIF_SAS_REMOTE_DEVICE_READY_SUBSTATE_NCQ_ERROR,

   SCIF_SAS_REMOTE_DEVICE_READY_SUBSTATE_MAX_STATES

} SCIF_SAS_REMOTE_DEVICE_READY_SUBSTATES;

struct SCIF_SAS_REMOTE_DEVICE;
typedef void (*SCIF_SAS_REMOTE_DEVICE_COMPLETION_HANDLER_T)(
   struct SCIF_SAS_REMOTE_DEVICE *,
   SCI_STATUS
);

typedef void (*SCIF_SAS_REMOTE_DEVICE_HANDLER_T)(
   struct SCIF_SAS_REMOTE_DEVICE *
);

typedef void (*SCIF_SAS_REMOTE_DEVICE_NOT_READY_HANDLER_T)(
   struct SCIF_SAS_REMOTE_DEVICE *,
   U32
);

/**
 * @struct SCIF_SAS_REMOTE_DEVICE_STATE_HANDLER
 *
 * @brief This structure defines the state handler methods for states and
 *        substates applicable for the framework remote device object.
 */
typedef struct SCIF_SAS_REMOTE_DEVICE_STATE_HANDLER
{
   SCI_BASE_REMOTE_DEVICE_STATE_HANDLER_T      parent;
   SCIF_SAS_REMOTE_DEVICE_COMPLETION_HANDLER_T start_complete_handler;
   SCIF_SAS_REMOTE_DEVICE_COMPLETION_HANDLER_T stop_complete_handler;
   SCIF_SAS_REMOTE_DEVICE_HANDLER_T            ready_handler;
   SCIF_SAS_REMOTE_DEVICE_NOT_READY_HANDLER_T  not_ready_handler;
   SCI_BASE_REMOTE_DEVICE_REQUEST_HANDLER_T    start_high_priority_io_handler;
   SCI_BASE_REMOTE_DEVICE_HIGH_PRIORITY_REQUEST_COMPLETE_HANDLER_T    complete_high_priority_io_handler;
} SCIF_SAS_REMOTE_DEVICE_STATE_HANDLER_T;

/**
 * @struct SCIF_SAS_REMOTE_DEVICE
 *
 * @brief The SCI SAS Framework remote device object abstracts the SAS remote
 *        device level behavior for the framework component.  Additionally,
 *        it provides a higher level of abstraction for the core remote
 *        device object.
 */
typedef struct SCIF_SAS_REMOTE_DEVICE
{
   /**
    * The SCI_BASE_REMOTE_DEVICE is the parent object for the
    * SCIF_SAS_REMOTE_DEVICE object.
    */
   SCI_BASE_REMOTE_DEVICE_T  parent;

   /**
    * This field contains the handle for the SCI Core remote device object
    * that is managed by this framework controller.
    */
   SCI_REMOTE_DEVICE_HANDLE_T  core_object;

   /**
    * This field references the list of state specific handler methods to
    * be utilized for this remote device instance.
    */
   SCIF_SAS_REMOTE_DEVICE_STATE_HANDLER_T * state_handlers;

   /**
    * This field specifies the state machine utilized to manage the
    * starting remote device substate machine.
    */
   SCI_BASE_STATE_MACHINE_T starting_substate_machine;

   /**
    * This field specifies the state machine utilized to manage the
    * starting remote device substate machine.
    */
   SCI_BASE_STATE_MACHINE_T ready_substate_machine;

   union
   {
      /**
       * This field specifies the information specific to SATA/STP device
       * instances.  This field is not utilized for SSP/SMP.
       */
      SCIF_SAS_STP_REMOTE_DEVICE_T  stp_device;

      /**
       * This field specifies the information specific to SMP device instances.
       * This field is not utilized for SSP/SATA/STP.
       */
      SCIF_SAS_SMP_REMOTE_DEVICE_T  smp_device;

   }protocol_device;

   /**
    * This field indicates the domain object containing this remote device.
    */
   struct SCIF_SAS_DOMAIN * domain;

   /**
    * This field counts the number of requests (IO and task management)
    * that are currently outstanding for this device.
    */
   U32 request_count;

   /**
    * This field counts the number of only task management request that are
    * currently outstanding for this device.
    */
   U32 task_request_count;

   /**
    * This field is utilize to store the status value of various operations
    * the can be executed on this remote device instance.
    */
   SCI_STATUS operation_status;

   /**
    * This field is utilize to indicate that the remote device should be
    * destructed when it finally reaches the stopped state.  This will
    * include destructing the core remote device as well.
    */
   BOOL  destruct_when_stopped;

   /**
    * This field marks a device state of being discovered or not, majorly used
    * during re-discover procedure.
    */
   BOOL is_currently_discovered;

   /**
    * This filed stores the expander device this device connected to, only if this
    * device is behind expander. So this field also served as a flag to tell if a
    * device is a EA one.
    */
   struct SCIF_SAS_REMOTE_DEVICE * containing_device;

   /**
    * This field stores the expander phy identifier for an expander attached
    * device. This field is only used by expander attached device.
    */
   U8 expander_phy_identifier;

   /**
    * This field stores the port width for a device. Most devices are narrow port
    * device, their port width is 1. If a device is a wide port device, their
    * port width is larger than 1.
    */
   U8 device_port_width;

   /**
    * This field stores the destination state for a remote device in UPDATING
    * PORT WIDTH state. The possible destination states for a remote device in
    * UPDATING_PORT_WIDTH state are READY or STOPPING.
    */
   U16 destination_state;

   /**
    * This field stores the scheduled/delayed EA target reset request.
    */
   struct SCIF_SAS_REQUEST * ea_target_reset_request_scheduled;

   #ifdef SCI_LOGGING
   /**
    * This field is the observer of the base state machine for this device
    * object.
    */
   SCI_BASE_OBSERVER_T base_state_machine_observer;

   /**
    * This field is the state machine logger of the startig substate machine for
    * this device object.
    */
   SCI_BASE_STATE_MACHINE_LOGGER_T starting_substate_machine_logger;

   /**
    * This field is the state machine logger of the ready substate machine for
    * this device object.
    */
   SCI_BASE_STATE_MACHINE_LOGGER_T ready_substate_machine_logger;
   #endif // SCI_LOGGING

} SCIF_SAS_REMOTE_DEVICE_T;

extern SCI_BASE_STATE_T scif_sas_remote_device_state_table[];
extern SCIF_SAS_REMOTE_DEVICE_STATE_HANDLER_T
   scif_sas_remote_device_state_handler_table[];

extern SCI_BASE_STATE_T scif_sas_remote_device_starting_substate_table[];
extern SCIF_SAS_REMOTE_DEVICE_STATE_HANDLER_T
   scif_sas_remote_device_starting_substate_handler_table[];

extern SCI_BASE_STATE_T scif_sas_remote_device_ready_substate_table[];
extern SCIF_SAS_REMOTE_DEVICE_STATE_HANDLER_T
   scif_sas_remote_device_ready_substate_handler_table[];

/**
 * @enum
 *
 * This enumeration is used to define the end destination state for the
 * framework remote device.
 */
enum SCIF_SAS_REMOTE_DEVICE_DESTINATION_STATE
{
   SCIF_SAS_REMOTE_DEVICE_DESTINATION_STATE_UNSPECIFIED,
   SCIF_SAS_REMOTE_DEVICE_DESTINATION_STATE_READY,
   SCIF_SAS_REMOTE_DEVICE_DESTINATION_STATE_STOPPING,
   SCIF_SAS_REMOTE_DEVICE_DESTINATION_STATE_UPDATING_PORT_WIDTH
};

//******************************************************************************
//* P R O T E C T E D   M E T H O D S
//******************************************************************************
void scif_sas_remote_device_save_report_phy_sata_information(
   SMP_RESPONSE_REPORT_PHY_SATA_T * report_phy_sata_response
);

void scif_sas_remote_device_target_reset(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   struct SCIF_SAS_REQUEST  * fw_request
);

void scif_sas_remote_device_target_reset_complete(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   struct SCIF_SAS_REQUEST  * fw_request,
   SCI_STATUS                 completion_status
);

#ifdef SCI_LOGGING
void scif_sas_remote_device_initialize_state_logging(
   SCIF_SAS_REMOTE_DEVICE_T * remote_device
);

void scif_sas_remote_device_deinitialize_state_logging(
   SCIF_SAS_REMOTE_DEVICE_T * remote_device
);
#else // SCI_LOGGING
#define scif_sas_remote_device_initialize_state_logging(x)
#define scif_sas_remote_device_deinitialize_state_logging(x)
#endif // SCI_LOGGING

//******************************************************************************
//* R E A D Y   O P E R A T I O N A L   S T A T E   H A N D L E R S
//******************************************************************************

SCI_STATUS scif_sas_remote_device_ready_operational_complete_io_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request
);

SCI_STATUS scif_sas_remote_device_ready_operational_complete_task_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * task_request
);

SCI_STATUS scif_sas_remote_device_ready_task_management_complete_task_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * task_request
);

//******************************************************************************
//* D E F A U L T   S T A T E   H A N D L E R S
//******************************************************************************

SCI_STATUS scif_sas_remote_device_default_start_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device
);

SCI_STATUS scif_sas_remote_device_default_stop_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device
);

SCI_STATUS scif_sas_remote_device_default_reset_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device
);

SCI_STATUS scif_sas_remote_device_default_reset_complete_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device
);

SCI_STATUS scif_sas_remote_device_default_start_io_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request
);

void scif_sas_remote_device_default_start_complete_handler(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   SCI_STATUS                 completion_status
);

void scif_sas_remote_device_default_stop_complete_handler(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   SCI_STATUS                 completion_status
);

SCI_STATUS scif_sas_remote_device_default_destruct_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device
);

SCI_STATUS scif_sas_remote_device_default_complete_io_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request
);

SCI_STATUS scif_sas_remote_device_default_complete_high_priority_io_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request,
   void                     * response_data,
   SCI_IO_STATUS              completion_status
);

SCI_STATUS scif_sas_remote_device_default_continue_io_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request
);

SCI_STATUS scif_sas_remote_device_default_start_task_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * task_request
);

SCI_STATUS scif_sas_remote_device_default_complete_task_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * task_request
);

void scif_sas_remote_device_default_ready_handler(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device
);

void scif_sas_remote_device_default_not_ready_handler(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   U32                        reason_code
);

SCI_STATUS scif_sas_remote_device_ready_task_management_start_high_priority_io_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request
);

SCI_STATUS scif_sas_remote_device_ready_task_management_complete_high_priority_io_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request,
   void                     * response_data,
   SCI_IO_STATUS              completion_status
);

#if !defined(DISABLE_WIDE_PORTED_TARGETS)
SCI_STATUS scif_sas_remote_device_update_port_width(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   U8                         new_port_width
);
#else // !defined(DISABLE_WIDE_PORTED_TARGETS)
#define scif_sas_remote_device_update_port_width(device) SCI_FAILURE
#endif //#if !defined(DISABLE_WIDE_PORTED_TARGETS)

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIF_SAS_REMOTE_DEVICE_H_

