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
#ifndef _SCIC_SDS_CONTROLLER_H_
#define _SCIC_SDS_CONTROLLER_H_

/**
 * @file
 *
 * @brief This file contains the structures, constants and prototypes used for
 *        the core controller object.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_pool.h>
#include <dev/isci/scil/sci_controller_constants.h>
#include <dev/isci/scil/sci_memory_descriptor_list.h>
#include <dev/isci/scil/sci_base_controller.h>
#include <dev/isci/scil/scic_config_parameters.h>
#include <dev/isci/scil/scic_sds_port.h>
#include <dev/isci/scil/scic_sds_phy.h>
#include <dev/isci/scil/scic_sds_remote_node_table.h>
#include <dev/isci/scil/scu_registers.h>
#include <dev/isci/scil/scu_constants.h>
#include <dev/isci/scil/scu_remote_node_context.h>
#include <dev/isci/scil/scu_task_context.h>
#include <dev/isci/scil/scu_unsolicited_frame.h>
#include <dev/isci/scil/scic_sds_unsolicited_frame_control.h>
#include <dev/isci/scil/scic_sds_port_configuration_agent.h>
#include <dev/isci/scil/scic_sds_pci.h>

struct SCIC_SDS_REMOTE_DEVICE;
struct SCIC_SDS_REQUEST;


#define SCU_COMPLETION_RAM_ALIGNMENT            (64)

/**
 * @enum SCIC_SDS_CONTROLLER_MEMORY_DESCRIPTORS
 *
 * This enumeration depects the types of MDEs that are going to be created for
 * the controller object.
 */
enum SCIC_SDS_CONTROLLER_MEMORY_DESCRIPTORS
{
   /**
    * Completion queue MDE entry
    */
   SCU_MDE_COMPLETION_QUEUE,

   /**
    * Remote node context MDE entry
    */
   SCU_MDE_REMOTE_NODE_CONTEXT,

   /**
    * Task context MDE entry
    */
   SCU_MDE_TASK_CONTEXT,

   /**
    * Unsolicited frame buffer MDE entrys this is the start of the unsolicited
    * frame buffer entries.
    */
   SCU_MDE_UF_BUFFER,

   SCU_MAX_MDES
};

/**
 * @struct SCIC_POWER_CONTROL
 *
 * This structure defines the fields for managing power control for direct
 * attached disk devices.
 */
typedef struct SCIC_POWER_CONTROL
{
   /**
    * This field is set when the power control timer is running and cleared when
    * it is not.
    */
   BOOL timer_started;

   /**
    * This field is the handle to the driver timer object.  This timer is used to
    * control when the directed attached disks can consume power.
    */
   void *timer;

   /**
   * This field is used to keep track of how many phys are put into the
   * requesters field.
   */
   U8 phys_waiting;

   /**
   * This field is used to keep track of how many remote devices have been granted to consume power
   */
   U8 remote_devices_granted_power;

   /**
    * This field is an array of phys that we are waiting on. The phys are direct
    * mapped into requesters via SCIC_SDS_PHY_T.phy_index
    */
   SCIC_SDS_PHY_T *requesters[SCI_MAX_PHYS];

} SCIC_POWER_CONTROL_T;

/**
 * @struct SCIC_SDS_CONTROLLER
 *
 * This structure represents the SCU contoller object.
 */
typedef struct SCIC_SDS_CONTROLLER
{
   /**
    * The SCI_BASE_CONTROLLER is the parent object for the SCIC_SDS_CONTROLLER
    * object.
    */
   SCI_BASE_CONTROLLER_T parent;

   /**
    * This field is the driver timer object handler used to time the controller
    * object start and stop requests.
    */
   void *timeout_timer;

   /**
    * This field is the current set of state handlers assigned to this controller
    * object.
    */
   struct SCIC_SDS_CONTROLLER_STATE_HANDLER *state_handlers;

   /**
    * This field contains the user parameters to be utilized for this
    * core controller object.
    */
   SCIC_USER_PARAMETERS_T  user_parameters;

   /**
    * This field contains the OEM parameters version defining the structure
    * layout. It comes from the version in the OEM block header.
    */
   U8 oem_parameters_version;

   /**
    * This field contains the OEM parameters to be utilized for this
    * core controller object.
    */
   SCIC_OEM_PARAMETERS_T  oem_parameters;

   /**
    * This field contains the port configuration agent for this controller.
    */
   SCIC_SDS_PORT_CONFIGURATION_AGENT_T port_agent;

   /**
    * This field is the array of port objects that are controlled by this
    * controller object.  There is one dummy port object also contained within
    * this controller object.
    */
   struct SCIC_SDS_PORT port_table[SCI_MAX_PORTS + 1];

   /**
    * This field is the array of phy objects that are controlled by this
    * controller object.
    */
   struct SCIC_SDS_PHY phy_table[SCI_MAX_PHYS];

   /**
    * This field is the array of device objects that are currently constructed
    * for this controller object.  This table is used as a fast lookup of device
    * objects that need to handle device completion notifications from the
    * hardware. The table is RNi based.
    */
   struct SCIC_SDS_REMOTE_DEVICE *device_table[SCI_MAX_REMOTE_DEVICES];

   /**
    * This field is the array of IO request objects that are currently active for
    * this controller object.  This table is used as a fast lookup of the io
    * request object that need to handle completion queue notifications.  The
    * table is TCi based.
    */
   struct SCIC_SDS_REQUEST *io_request_table[SCI_MAX_IO_REQUESTS];

   /**
    * This field is the free RNi data structure
    */
   SCIC_REMOTE_NODE_TABLE_T available_remote_nodes;

   /**
    * This field is the TCi pool used to manage the task context index.
    */
   SCI_POOL_CREATE(tci_pool, U16, SCI_MAX_IO_REQUESTS);

   /**
    * This filed is the SCIC_POWER_CONTROL data used to control when direct
    * attached devices can consume power.
    */
   SCIC_POWER_CONTROL_T power_control;

   /**
    * This field is the array of sequence values for the IO Tag fields.  Even
    * though only 4 bits of the field is used for the sequence the sequence is 16
    * bits in size so the sequence can be bitwise or'd with the TCi to build the
    * IO Tag value.
    */
   U16 io_request_sequence[SCI_MAX_IO_REQUESTS];

   /**
    * This field in the array of sequence values for the RNi.  These are used
    * to control io request build to io request start operations.  The sequence
    * value is recorded into an io request when it is built and is checked on
    * the io request start operation to make sure that there was not a device
    * hot plug between the build and start operation.
    */
   U8  remote_device_sequence[SCI_MAX_REMOTE_DEVICES];

   /**
    * This field is a pointer to the memory allocated by the driver for the task
    * context table.  This data is shared between the hardware and software.
    */
   SCU_TASK_CONTEXT_T *task_context_table;

   /**
    * This field is a pointer to the memory allocated by the driver for the
    * remote node context table.  This table is shared between the hardware and
    * software.
    */
   SCU_REMOTE_NODE_CONTEXT_T *remote_node_context_table;

   /**
    * This field is the array of physical memory requiremets for this controller
    * object.
    */
   SCI_PHYSICAL_MEMORY_DESCRIPTOR_T memory_descriptors[SCU_MAX_MDES];

   /**
    * This field is a pointer to the completion queue.  This memory is
    * written to by the hardware and read by the software.
    */
   U32 *completion_queue;

   /**
    * This field is the software copy of the completion queue get pointer.  The
    * controller object writes this value to the hardware after processing the
    * completion entries.
    */
   U32 completion_queue_get;

   /**
    * This field is the minimum of the number of hardware supported port entries
    * and the software requested port entries.
    */
   U32 logical_port_entries;

   /**
    * This field is the minimum number of hardware supported completion queue
    * entries and the software requested completion queue entries.
    */
   U32 completion_queue_entries;

   /**
    * This field is the minimum number of hardware supported event entries and
    * the software requested event entries.
    */
   U32 completion_event_entries;

   /**
    * This field is the minimum number of devices supported by the hardware and
    * the number of devices requested by the software.
    */
   U32 remote_node_entries;

   /**
    * This field is the minimum number of IO requests supported by the hardware
    * and the number of IO requests requested by the software.
    */
   U32 task_context_entries;

   /**
    * This object contains all of the unsolicited frame specific
    * data utilized by the core controller.
    */
   SCIC_SDS_UNSOLICITED_FRAME_CONTROL_T uf_control;

   /**
    * This field records the fact that the controller has encountered a fatal
    * error and must be reset.
    */
   BOOL encountered_fatal_error;

   /**
    * This field specifies that the controller should ignore
    * completion processing for non-fastpath events.  This will
    * cause the completions to be thrown away.
    */
   BOOL restrict_completions;

   // Phy Startup Data
   /**
    * This field is the driver timer handle for controller phy request startup.
    * On controller start the controller will start each PHY individually in
    * order of phy index.
    */
   void *phy_startup_timer;

   /**
    * This field is set when the phy_startup_timer is running and is cleared when
    * the phy_startup_timer is stopped.
    */
   BOOL phy_startup_timer_pending;

   /**
    * This field is the index of the next phy start.  It is initialized to 0 and
    * increments for each phy index that is started.
    */
   U32 next_phy_to_start;

   /**
    * This field controls the invalid link up notifications to the SCI_USER.  If
    * an invalid_link_up notification is reported a bit for the PHY index is set
    * so further notifications are not made.  Once the PHY object reports link up
    * and is made part of a port then this bit for the PHY index is cleared.
    */
   U8  invalid_phy_mask;

   /**
    * This is the controller index for this controller object.
    */
   U8 controller_index;

   /**
    * This field is the PCI revision code for the controller object.
    */
   enum SCU_CONTROLLER_PCI_REVISION_CODE pci_revision;

   /*
    * This field saves the current interrupt coalescing number of the controller.
    */
   U16 interrupt_coalesce_number;

   /*
    * This field saves the current interrupt coalescing timeout value in microseconds.
    */
   U32 interrupt_coalesce_timeout;

   // Hardware memory mapped register space
#ifdef ARLINGTON_BUILD
   /**
    * This field is a pointer to the memory mapped register space for the
    * LEX_REGISTERS.
    */
   LEX_REGISTERS_T *lex_registers;
#endif

   /**
    * This field is a pointer to the memory mapped register space for the
    * SMU_REGISTERS.
    */
   SMU_REGISTERS_T *smu_registers;

   /**
    * This field is a pointer to the memory mapped register space for the
    * SCU_REGISTERS.
    */
   SCU_REGISTERS_T *scu_registers;

} SCIC_SDS_CONTROLLER_T;


typedef void (*SCIC_SDS_CONTROLLER_PHY_HANDLER_T)(
                     struct SCIC_SDS_CONTROLLER *controller,
                     struct SCIC_SDS_PORT       *port,
                     struct SCIC_SDS_PHY        *phy
                     );

typedef void (*SCIC_SDS_CONTROLLER_DEVICE_HANDLER_T)(
                     struct SCIC_SDS_CONTROLLER    * controller,
                     struct SCIC_SDS_REMOTE_DEVICE * device
                     );
/**
 * @struct SCIC_SDS_CONTROLLER_STATE_HANDLER
 *
 * This structure contains the SDS core specific definition for the state
 * handlers.
 */
typedef struct SCIC_SDS_CONTROLLER_STATE_HANDLER
{
   SCI_BASE_CONTROLLER_STATE_HANDLER_T parent;

   SCI_BASE_CONTROLLER_REQUEST_HANDLER_T terminate_request_handler;
   SCIC_SDS_CONTROLLER_PHY_HANDLER_T     link_up_handler;
   SCIC_SDS_CONTROLLER_PHY_HANDLER_T     link_down_handler;
   SCIC_SDS_CONTROLLER_DEVICE_HANDLER_T  remote_device_started_handler;
   SCIC_SDS_CONTROLLER_DEVICE_HANDLER_T  remote_device_stopped_handler;

} SCIC_SDS_CONTROLLER_STATE_HANDLER_T;

extern SCIC_SDS_CONTROLLER_STATE_HANDLER_T
       scic_sds_controller_state_handler_table[];
extern SCI_BASE_STATE_T scic_sds_controller_state_table[];

/**
 * This macro will increment the specified index to and if the index wraps
 * to 0 it will toggel the cycle bit.
 */
#define INCREMENT_QUEUE_GET(index, cycle, entry_count, bit_toggle) \
{ \
   if ((index) + 1 == entry_count) \
   { \
      (index) = 0; \
      (cycle) = (cycle) ^ (bit_toggle); \
   } \
   else \
   { \
      index = index + 1; \
   } \
}

/**
 * This is a helper macro that sets the state handlers for the controller
 * object
 */
#define scic_sds_controller_set_state_handlers(this_controller, handlers) \
   ((this_controller)->state_handlers = (handlers))

/**
 * This is a helper macro that gets the base state machine for the
 * controller object
 */
#define scic_sds_controller_get_base_state_machine(this_contoroller) \
   (&(this_controller)->parent.state_machine)

/**
 * This is a helper macro to get the port configuration agent from the
 * controller object.
 */
#define scic_sds_controller_get_port_configuration_agent(controller) \
   (&(controller)->port_agent)

/**
 * This is a helper macro that sets the base state machine state handlers
 * based on the state id
 */
#define scic_sds_controller_set_base_state_handlers(this_controller, state_id) \
   scic_sds_controller_set_state_handlers( \
      this_controller, &scic_sds_controller_state_handler_table[(state_id)])

/**
 * This macro writes to the smu_register for this controller
 */
#define smu_register_write(controller, reg, value) \
   scic_sds_pci_write_smu_dword((controller), &(reg), (value))

/**
 * This macro reads the smu_register for this controller
 */
#define smu_register_read(controller, reg) \
   scic_sds_pci_read_smu_dword((controller), &(reg))

/**
 * This mcaro writes the scu_register for this controller
 */
#define scu_register_write(controller, reg, value) \
   scic_sds_pci_write_scu_dword((controller), &(reg), (value))

/**
 * This macro reads the scu_register for this controller
 */
#define scu_register_read(controller, reg) \
   scic_sds_pci_read_scu_dword((controller), &(reg))

#ifdef ARLINGTON_BUILD
   /**
    * This macro writes to the lex_register for this controller.
    */
   #define lex_register_write(controller, reg, value) \
      scic_cb_pci_write_dword((controller), (reg), (value))

   /**
    * This macro reads from the lex_register for this controller.
    */
   #define lex_register_read(controller, reg) \
      scic_cb_pci_read_dword((controller), (reg))
#endif // ARLINGTON_BUILD

/**
 * This macro returns the protocol engine group for this controller object.
 * Presently we only support protocol engine group 0 so just return that
 */
#define scic_sds_controller_get_protocol_engine_group(controller) 0

/**
 * This macro constructs an IO tag from the sequence and index values.
 */
#define scic_sds_io_tag_construct(sequence, task_index) \
   ((sequence) << 12 | (task_index))

/**
 * This macro returns the IO sequence from the IO tag value.
 */
#define scic_sds_io_tag_get_sequence(io_tag) \
   (((io_tag) & 0xF000) >> 12)

/**
 * This macro returns the TCi from the io tag value
 */
#define scic_sds_io_tag_get_index(io_tag) \
   ((io_tag) & 0x0FFF)

/**
 * This is a helper macro to increment the io sequence count.
 *
 * We may find in the future that it will be faster to store the sequence
 * count in such a way as we dont perform the shift operation to build io
 * tag values so therefore need a way to incrment them correctly
 */
#define scic_sds_io_sequence_increment(value) \
   ((value) = (((value) + 1) & 0x000F))

#define scic_sds_remote_device_node_count(device) \
   ( \
      ( \
         (device)->target_protocols.u.bits.attached_stp_target \
      && ((device)->is_direct_attached != TRUE) \
      ) \
      ? SCU_STP_REMOTE_NODE_COUNT : SCU_SSP_REMOTE_NODE_COUNT \
   )

/**
 * This macro will set the bit in the invalid phy mask for this controller
 * object.  This is used to control messages reported for invalid link up
 * notifications.
 */
#define scic_sds_controller_set_invalid_phy(controller, phy) \
   ((controller)->invalid_phy_mask |= (1 << (phy)->phy_index))

/**
 * This macro will clear the bit in the invalid phy mask for this controller
 * object.  This is used to control messages reported for invalid link up
 * notifications.
 */
#define scic_sds_controller_clear_invalid_phy(controller, phy) \
   ((controller)->invalid_phy_mask &= ~(1 << (phy)->phy_index))

// ---------------------------------------------------------------------------

U32 scic_sds_controller_get_object_size(void);

// ---------------------------------------------------------------------------

U32 scic_sds_controller_get_min_timer_count(void);
U32 scic_sds_controller_get_max_timer_count(void);

// ---------------------------------------------------------------------------

void scic_sds_controller_post_request(
   SCIC_SDS_CONTROLLER_T *this_controller,
   U32                    request
);

// ---------------------------------------------------------------------------

void scic_sds_controller_release_frame(
   SCIC_SDS_CONTROLLER_T *this_controller,
   U32                   frame_index
);

void scic_sds_controller_copy_sata_response(
   void * response_buffer,
   void * frame_header,
   void * frame_buffer
);

// ---------------------------------------------------------------------------

SCI_STATUS scic_sds_controller_allocate_remote_node_context(
   SCIC_SDS_CONTROLLER_T         *this_controller,
   struct SCIC_SDS_REMOTE_DEVICE *the_device,
   U16                           *node_id
);

void scic_sds_controller_free_remote_node_context(
   SCIC_SDS_CONTROLLER_T         *this_controller,
   struct SCIC_SDS_REMOTE_DEVICE *the_device,
   U16                            node_id
);

SCU_REMOTE_NODE_CONTEXT_T *scic_sds_controller_get_remote_node_context_buffer(
   SCIC_SDS_CONTROLLER_T *this_controller,
   U16                    node_id
);

// ---------------------------------------------------------------------------

struct SCIC_SDS_REQUEST *scic_sds_controller_get_io_request_from_tag(
   SCIC_SDS_CONTROLLER_T *this_controller,
   U16                    io_tag
);

U16 scic_sds_controller_get_io_sequence_from_tag(
   SCIC_SDS_CONTROLLER_T *this_controller,
   U16                    io_tag
);

SCU_TASK_CONTEXT_T *scic_sds_controller_get_task_context_buffer(
   SCIC_SDS_CONTROLLER_T *this_controller,
   U16                   io_tag
);

//-----------------------------------------------------------------------------

SCI_STATUS scic_sds_terminate_reqests(
        SCIC_SDS_CONTROLLER_T *this_controller,
        struct SCIC_SDS_REMOTE_DEVICE *this_remote_device,
        struct SCIC_SDS_PORT *this_port
);

//*****************************************************************************
//* CORE CONTROLLER POWER CONTROL METHODS
//*****************************************************************************

void scic_sds_controller_power_control_timer_handler(
   void *controller
);

void scic_sds_controller_power_control_queue_insert(
   SCIC_SDS_CONTROLLER_T *this_controller,
   struct SCIC_SDS_PHY   *the_phy
);

void scic_sds_controller_power_control_queue_remove(
   SCIC_SDS_CONTROLLER_T *this_controller,
   struct SCIC_SDS_PHY   *the_phy
);

//*****************************************************************************
//* CORE CONTROLLER PHY MESSAGE PROCESSING
//*****************************************************************************

void scic_sds_controller_link_up(
   SCIC_SDS_CONTROLLER_T *this_controller,
   struct SCIC_SDS_PORT  *the_port,
   struct SCIC_SDS_PHY   *the_phy
);

void scic_sds_controller_link_down(
   SCIC_SDS_CONTROLLER_T *this_controller,
   struct SCIC_SDS_PORT  *the_port,
   struct SCIC_SDS_PHY   *the_phy
);

//*****************************************************************************
//* CORE CONTROLLER PORT AGENT MESSAGE PROCESSING
//*****************************************************************************
void scic_sds_controller_port_agent_configured_ports(
   SCIC_SDS_CONTROLLER_T * this_controller
);

//*****************************************************************************
//* CORE CONTROLLER REMOTE DEVICE MESSAGE PROCESSING
//*****************************************************************************

BOOL scic_sds_controller_has_remote_devices_stopping(
   SCIC_SDS_CONTROLLER_T * this_controller
);

void scic_sds_controller_remote_device_started(
   SCIC_SDS_CONTROLLER_T         * this_controller,
   struct SCIC_SDS_REMOTE_DEVICE * the_device
);

void scic_sds_controller_remote_device_stopped(
   SCIC_SDS_CONTROLLER_T         * this_controller,
   struct SCIC_SDS_REMOTE_DEVICE * the_device
);

//*****************************************************************************
//* CORE CONTROLLER PRIVATE METHODS
//*****************************************************************************

#ifdef SCI_LOGGING
void scic_sds_controller_initialize_state_logging(
   SCIC_SDS_CONTROLLER_T *this_controller
);

void scic_sds_controller_deinitialize_state_logging(
   SCIC_SDS_CONTROLLER_T *this_controller
);
#else
#define scic_sds_controller_initialize_state_logging(x)
#define scic_sds_controller_deinitialize_state_logging(x)
#endif

SCI_STATUS scic_sds_controller_validate_memory_descriptor_table(
   SCIC_SDS_CONTROLLER_T *this_controller
);

void scic_sds_controller_ram_initialization(
   SCIC_SDS_CONTROLLER_T *this_controller
);

void scic_sds_controller_assign_task_entries(
   SCIC_SDS_CONTROLLER_T *this_controller
);

void scic_sds_controller_afe_initialization(
   SCIC_SDS_CONTROLLER_T * this_controller
);

void scic_sds_controller_enable_port_task_scheduler(
   SCIC_SDS_CONTROLLER_T *this_controller
);

void scic_sds_controller_initialize_completion_queue(
   SCIC_SDS_CONTROLLER_T *this_controller
);

void scic_sds_controller_initialize_unsolicited_frame_queue(
   SCIC_SDS_CONTROLLER_T *this_controller
);

void scic_sds_controller_phy_timer_stop(
   SCIC_SDS_CONTROLLER_T *this_controller
);

BOOL scic_sds_controller_is_start_complete(
   SCIC_SDS_CONTROLLER_T *this_controller
);

SCI_STATUS scic_sds_controller_start_next_phy(
   SCIC_SDS_CONTROLLER_T *this_controller
);

SCI_STATUS scic_sds_controller_stop_phys(
   SCIC_SDS_CONTROLLER_T *this_controller
);

SCI_STATUS scic_sds_controller_stop_ports(
   SCIC_SDS_CONTROLLER_T *this_controller
);

SCI_STATUS scic_sds_controller_stop_devices(
   SCIC_SDS_CONTROLLER_T *this_controller
);

void scic_sds_controller_copy_task_context(
   SCIC_SDS_CONTROLLER_T   *this_controller,
   struct SCIC_SDS_REQUEST *this_request
);

void scic_sds_controller_timeout_handler(
   SCI_CONTROLLER_HANDLE_T controller
);

void scic_sds_controller_initialize_power_control(
   SCIC_SDS_CONTROLLER_T *this_controller
);

void scic_sds_controller_register_setup(
   SCIC_SDS_CONTROLLER_T *this_controller
);

void scic_sds_controller_reset_hardware(
   SCIC_SDS_CONTROLLER_T * this_controller
);

#ifdef ARLINGTON_BUILD
void scic_sds_controller_lex_atux_initialization(
   SCIC_SDS_CONTROLLER_T *this_controller
);

void scic_sds_controller_enable_chipwatch(
   SCIC_SDS_CONTROLLER_T *this_controller
);
#endif // ARLINGTON_BUILD

void scic_sds_controller_build_memory_descriptor_table(
   SCIC_SDS_CONTROLLER_T *this_controller
);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIC_SDS_CONTROLLER_H_
