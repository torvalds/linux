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
#ifndef _SCIC_SDS_PORT_H_
#define _SCIC_SDS_PORT_H_

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/**
 * @file
 *
 * @brief This file contains the structures, constants and prototypes for the
 * SCIC_SDS_PORT_T object.
 */

#include <dev/isci/scil/sci_controller_constants.h>
#include <dev/isci/scil/intel_sas.h>
#include <dev/isci/scil/sci_base_port.h>
#include <dev/isci/scil/sci_base_phy.h>
#include <dev/isci/scil/scu_registers.h>

#define SCIC_SDS_DUMMY_PORT   0xFF

/**
 * @enum SCIC_SDS_PORT_READY_SUBSTATES
 *
 * This enumeration depicts all of the states for the core port ready substate
 * machine.
 */
enum SCIC_SDS_PORT_READY_SUBSTATES
{
   /**
    * The substate where the port is started and ready but has no active phys.
    */
   SCIC_SDS_PORT_READY_SUBSTATE_WAITING,

   /**
    * The substate where the port is started and ready and there is at least one
    * phy operational.
    */
   SCIC_SDS_PORT_READY_SUBSTATE_OPERATIONAL,

   /**
    * The substate where the port is started and there was an add/remove phy
    * event.  This state is only used in Automatic Port Configuration Mode (APC)
    */
   SCIC_SDS_PORT_READY_SUBSTATE_CONFIGURING,

   SCIC_SDS_PORT_READY_MAX_SUBSTATES
};

struct SCIC_SDS_CONTROLLER;
struct SCIC_SDS_PHY;
struct SCIC_SDS_REMOTE_DEVICE;
struct SCIC_SDS_REQUEST;

/**
 * @struct SCIC_SDS_PORT
 *
 * The core port object provides the abstraction for an SCU port.
 */
typedef struct SCIC_SDS_PORT
{
   /**
    * This field is the oommon base port object.
    */
   SCI_BASE_PORT_T parent;

   /**
    * This field is the port index that is reported to the SCI USER.  This allows
    * the actual hardware physical port to change without the SCI USER getting a
    * different answer for the get port index.
    */
   U8 logical_port_index;

   /**
    * This field is the port index used to program the SCU hardware.
    */
   U8 physical_port_index;

   /**
    * This field contains the active phy mask for the port.  This mask is used in
    * conjunction with the phy state to determine which phy to select for some
    * port operations.
    */
   U8 active_phy_mask;

   /**
    * This field contains the phy mask for the port that are already part of the port.
   */
   U8 enabled_phy_mask;

   U16 reserved_rni;
   U16 reserved_tci;

   /**
    * This field contains the count of the io requests started on this port
    * object.  It is used to control controller shutdown.
    */
   U32 started_request_count;

   /**
    * This field contains the number of devices assigned to this port.  It is
    * used to control port start requests.
    */
   U32 assigned_device_count;

   /**
    * This field contains the reason for the port not going ready.  It is
    * assigned in the state handlers and used in the state transition.
    */
   U32 not_ready_reason;

   /**
    * This field is the table of phys assigned to the port.
    */
   struct SCIC_SDS_PHY *phy_table[SCI_MAX_PHYS];

   /**
    * This field is a pointer back to the controller that owns this port object.
    */
   struct SCIC_SDS_CONTROLLER *owning_controller;

   /**
    * This field contains the port start/stop timer handle.
    */
   void *timer_handle;

   /**
    * This field points to the current set of state handlers for this port
    * object.  These state handlers are assigned at each enter state of the state
    * machine.
    */
   struct SCIC_SDS_PORT_STATE_HANDLER *state_handlers;

   /**
    * This field is the ready substate machine for the port.
    */
   SCI_BASE_STATE_MACHINE_T ready_substate_machine;

   #ifdef SCI_LOGGING
   /**
    * This field is the ready substate machine logger.  It logs each state
    * transition request in the ready substate machine.
    */
   SCI_BASE_STATE_MACHINE_LOGGER_T ready_substate_machine_logger;
   #endif

   /// Memory mapped hardware register space

   /**
    * This field is the pointer to the port task scheduler registers for the SCU
    * hardware.
    */
   SCU_PORT_TASK_SCHEDULER_REGISTERS_T *port_task_scheduler_registers;

   /**
    * This field is identical for all port objects and points to the port task
    * scheduler group PE configuration registers.  It is used to assign PEs to a
    * port.
    */
   SCU_PORT_PE_CONFIGURATION_REGISTER_T *port_pe_configuration_register;

   /**
    * This field is the VIIT register space for this port object.
    */
   SCU_VIIT_ENTRY_T *viit_registers;

} SCIC_SDS_PORT_T;


typedef SCI_STATUS (*SCIC_SDS_PORT_EVENT_HANDLER_T)(struct SCIC_SDS_PORT *, U32);

typedef SCI_STATUS (*SCIC_SDS_PORT_FRAME_HANDLER_T)(struct SCIC_SDS_PORT *, U32);

typedef void (*SCIC_SDS_PORT_LINK_HANDLER_T)(struct SCIC_SDS_PORT *, struct SCIC_SDS_PHY *);

typedef SCI_STATUS (*SCIC_SDS_PORT_IO_REQUEST_HANDLER_T)(
                           struct SCIC_SDS_PORT *,
                           struct SCIC_SDS_REMOTE_DEVICE *,
                           struct SCIC_SDS_REQUEST *);

typedef struct SCIC_SDS_PORT_STATE_HANDLER
{
   SCI_BASE_PORT_STATE_HANDLER_T parent;

   SCIC_SDS_PORT_FRAME_HANDLER_T frame_handler;
   SCIC_SDS_PORT_EVENT_HANDLER_T event_handler;

   SCIC_SDS_PORT_LINK_HANDLER_T link_up_handler;
   SCIC_SDS_PORT_LINK_HANDLER_T link_down_handler;

   SCIC_SDS_PORT_IO_REQUEST_HANDLER_T start_io_handler;
   SCIC_SDS_PORT_IO_REQUEST_HANDLER_T complete_io_handler;

} SCIC_SDS_PORT_STATE_HANDLER_T;

extern SCI_BASE_STATE_T scic_sds_port_state_table[];
extern SCI_BASE_STATE_T scic_sds_port_ready_substate_table[];

extern SCIC_SDS_PORT_STATE_HANDLER_T scic_sds_port_state_handler_table[];
extern SCIC_SDS_PORT_STATE_HANDLER_T scic_sds_port_ready_substate_handler_table[];

/**
 * Helper macro to get the owning controller of this port
 */
#define scic_sds_port_get_controller(this_port) \
   ((this_port)->owning_controller)

/**
 * Helper macro to get the base state machine for this port
 */
#define scic_sds_port_get_base_state_machine(this_port) \
   (&(this_port)->parent.state_machine)

/**
 * This macro will change the state handlers to those of the specified state
 * id
 */
#define scic_sds_port_set_base_state_handlers(this_port, state_id) \
   scic_sds_port_set_state_handlers( \
      (this_port), &scic_sds_port_state_handler_table[(state_id)])

/**
 * Helper macro to get the ready substate machine for this port
 */
#define scic_sds_port_get_ready_substate_machine(this_port) \
   (&(this_port)->ready_substate_machine)

/**
 * Helper macro to set the port object state handlers
 */
#define scic_sds_port_set_state_handlers(this_port, handlers) \
   ((this_port)->state_handlers = (handlers))

/**
 * This macro returns the physical port index for this port object
 */
#define scic_sds_port_get_index(this_port) \
   ((this_port)->physical_port_index)

/**
 * Helper macro to increment the started request count
 */
#define scic_sds_port_increment_request_count(this_port) \
   ((this_port)->started_request_count++)

#ifdef SCIC_DEBUG_ENABLED
/**
 * @brief This method decrements the started io request count.  The method
 *        will not decrment the started io request count below 0 and will
 *        log a debug message if this is attempted.
 *
 * @param[in] this_port
 */
void scic_sds_port_decrement_request_count(
   SCIC_SDS_PORT_T *this_port
);
#else
/**
 * Helper macro to decrement the started io request count.  The macro will
 * not decrement the started io request count below 0.
 */
#define scic_sds_port_decrement_request_count(this_port) \
   ( \
      (this_port)->started_request_count = ( \
                  ((this_port)->started_request_count == 0) ? \
                                  (this_port)->started_request_count : \
                                  ((this_port)->started_request_count - 1) \
                                              ) \
    )
#endif

/**
 * Helper macro to write the phys port assignment
 */
#define scic_sds_port_write_phy_assignment(port, phy) \
   SCU_PCSPExCR_WRITE( \
      (port), \
      (phy)->phy_index, \
      (port)->physical_port_index \
   )

/**
 * Helper macro to read the phys port assignment
 */
#define scic_sds_port_read_phy_assignment(port, phy) \
   SCU_PCSPExCR_READ( \
      (port), \
      (phy)->phy_index \
   )

#define scic_sds_port_active_phy(port, phy) \
   (((port)->active_phy_mask & (1 << (phy)->phy_index)) != 0)

// ---------------------------------------------------------------------------

U32 scic_sds_port_get_object_size(void);

U32 scic_sds_port_get_min_timer_count(void);

U32 scic_sds_port_get_max_timer_count(void);

// ---------------------------------------------------------------------------

#ifdef SCI_LOGGING
void scic_sds_port_initialize_state_logging(
   SCIC_SDS_PORT_T *this_port
);
#else
#define scic_sds_port_initialize_state_logging(x)
#endif

// ---------------------------------------------------------------------------

void scic_sds_port_construct(
   SCIC_SDS_PORT_T            *this_port,
   U8                          port_index,
   struct SCIC_SDS_CONTROLLER *owning_controller
);

SCI_STATUS scic_sds_port_initialize(
   SCIC_SDS_PORT_T *this_port,
   void *port_task_scheduler_registers,
   void *port_configuration_regsiter,
   void *viit_registers
);

// ---------------------------------------------------------------------------

SCI_STATUS scic_sds_port_add_phy(
   struct SCIC_SDS_PORT * this_port,
   struct SCIC_SDS_PHY  * the_phy
);

SCI_STATUS scic_sds_port_remove_phy(
   struct SCIC_SDS_PORT * this_port,
   struct SCIC_SDS_PHY  * the_phy
);

void scic_sds_port_setup_transports(
   SCIC_SDS_PORT_T * this_port,
   U32               device_id
);

void scic_sds_port_activate_phy(
   SCIC_SDS_PORT_T     *this_port,
   struct SCIC_SDS_PHY *phy,
   BOOL                 do_notify_user,
   BOOL                 do_resume_phy
);

void scic_sds_port_deactivate_phy(
   SCIC_SDS_PORT_T     *this_port,
   struct SCIC_SDS_PHY *phy,
   BOOL                 do_notify_user
);

struct SCIC_SDS_PHY * scic_sds_port_get_a_connected_phy(
   SCIC_SDS_PORT_T * this_port
);

void scic_sds_port_invalid_link_up(
   SCIC_SDS_PORT_T *this_port,
   struct SCIC_SDS_PHY *phy
);

void scic_sds_port_general_link_up_handler(
   SCIC_SDS_PORT_T     *this_port,
   struct SCIC_SDS_PHY *the_phy,
   BOOL                 do_notify_user,
   BOOL                 do_resume_phy
);

BOOL scic_sds_port_link_detected(
   SCIC_SDS_PORT_T *this_port,
   struct SCIC_SDS_PHY *phy
);

void scic_sds_port_link_up(
   SCIC_SDS_PORT_T *this_port,
   struct SCIC_SDS_PHY *phy
);

void scic_sds_port_link_down(
   SCIC_SDS_PORT_T *this_port,
   struct SCIC_SDS_PHY *phy
);

// ---------------------------------------------------------------------------

void scic_sds_port_timeout_handler(
   void *port
);

// ---------------------------------------------------------------------------

SCI_STATUS scic_sds_port_start_io(
   SCIC_SDS_PORT_T               *this_port,
   struct SCIC_SDS_REMOTE_DEVICE *the_device,
   struct SCIC_SDS_REQUEST       *the_io_request
);

SCI_STATUS scic_sds_port_complete_io(
   SCIC_SDS_PORT_T               *this_port,
   struct SCIC_SDS_REMOTE_DEVICE *the_device,
   struct SCIC_SDS_REQUEST       *the_io_request
);

// ---------------------------------------------------------------------------

void scic_sds_port_update_viit_entry(
   SCIC_SDS_PORT_T *this_port
);

// ---------------------------------------------------------------------------

SCI_STATUS scic_sds_port_default_start_handler(
   SCI_BASE_PORT_T *port
);

SCI_STATUS scic_sds_port_default_stop_handler(
   SCI_BASE_PORT_T *port
);

SCI_STATUS scic_sds_port_default_destruct_handler(
   SCI_BASE_PORT_T *port
);

SCI_STATUS scic_sds_port_default_reset_handler(
   SCI_BASE_PORT_T * port,
   U32               timeout
);

SCI_STATUS scic_sds_port_default_add_phy_handler(
   SCI_BASE_PORT_T *port,
   SCI_BASE_PHY_T  *phy
);

SCI_STATUS scic_sds_port_default_remove_phy_handler(
   SCI_BASE_PORT_T *port,
   SCI_BASE_PHY_T  *phy
);

SCI_STATUS scic_sds_port_default_frame_handler(
   struct SCIC_SDS_PORT * port,
   U32                    frame_index
);

SCI_STATUS scic_sds_port_default_event_handler(
   struct SCIC_SDS_PORT * port,
   U32                    event_code
);

void scic_sds_port_default_link_up_handler(
   struct SCIC_SDS_PORT *this_port,
   struct SCIC_SDS_PHY  *phy
);

void scic_sds_port_default_link_down_handler(
   struct SCIC_SDS_PORT *this_port,
   struct SCIC_SDS_PHY  *phy
);

SCI_STATUS scic_sds_port_default_start_io_handler(
   struct SCIC_SDS_PORT          *port,
   struct SCIC_SDS_REMOTE_DEVICE *device,
   struct SCIC_SDS_REQUEST       *io_request
);

SCI_STATUS scic_sds_port_default_complete_io_handler(
   struct SCIC_SDS_PORT          *port,
   struct SCIC_SDS_REMOTE_DEVICE *device,
   struct SCIC_SDS_REQUEST       *io_request
);

SCI_SAS_LINK_RATE scic_sds_port_get_max_allowed_speed(
   SCIC_SDS_PORT_T * this_port
);

void scic_sds_port_broadcast_change_received(
   struct SCIC_SDS_PORT * this_port,
   struct SCIC_SDS_PHY * this_phy
);

BOOL scic_sds_port_is_valid_phy_assignment(
   SCIC_SDS_PORT_T *this_port,
   U32              phy_index
);

BOOL scic_sds_port_is_phy_mask_valid(
   SCIC_SDS_PORT_T * this_port,
   U32               phy_mask
);

U32 scic_sds_port_get_phys(
   SCIC_SDS_PORT_T * this_port
);

void scic_sds_port_get_sas_address(
   SCIC_SDS_PORT_T   * this_port,
   SCI_SAS_ADDRESS_T * sas_address
);

void scic_sds_port_get_attached_sas_address(
   SCIC_SDS_PORT_T   * this_port,
   SCI_SAS_ADDRESS_T * sas_address
);

void scic_sds_port_get_attached_protocols(
   SCIC_SDS_PORT_T                            * this_port,
   SCI_SAS_IDENTIFY_ADDRESS_FRAME_PROTOCOLS_T * protocols
);

SCI_STATUS scic_sds_port_set_phy(
   struct SCIC_SDS_PORT *port,
   struct SCIC_SDS_PHY  *phy
);

SCI_STATUS scic_sds_port_clear_phy(
   struct SCIC_SDS_PORT *port,
   struct SCIC_SDS_PHY  *phy
);

void scic_sds_port_suspend_port_task_scheduler(
   SCIC_SDS_PORT_T *this_port
);

void scic_sds_port_resume_port_task_scheduler(
   SCIC_SDS_PORT_T *this_port
);

void scic_sds_port_release_resource(
   struct SCIC_SDS_CONTROLLER * controller,
   struct SCIC_SDS_PORT       * port
);

#ifdef __cplusplus
}
#endif // __cplusplus


#endif // _SCIC_SDS_PORT_H_
