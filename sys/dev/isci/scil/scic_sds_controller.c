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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/**
 * @file
 *
 * @brief This file contains the implementation of the SCIC_SDS_CONTROLLER
 *        public, protected, and private methods.
 */

#include <dev/isci/types.h>
#include <dev/isci/scil/sci_util.h>
#include <dev/isci/scil/scic_controller.h>
#include <dev/isci/scil/scic_port.h>
#include <dev/isci/scil/scic_phy.h>
#include <dev/isci/scil/scic_remote_device.h>
#include <dev/isci/scil/scic_user_callback.h>
#include <dev/isci/scil/scic_sds_pci.h>
#include <dev/isci/scil/scic_sds_library.h>
#include <dev/isci/scil/scic_sds_controller.h>
#include <dev/isci/scil/scic_sds_controller_registers.h>
#include <dev/isci/scil/scic_sds_port.h>
#include <dev/isci/scil/scic_sds_phy.h>
#include <dev/isci/scil/scic_sds_remote_device.h>
#include <dev/isci/scil/scic_sds_request.h>
#include <dev/isci/scil/scic_sds_logger.h>
#include <dev/isci/scil/scic_sds_port_configuration_agent.h>
#include <dev/isci/scil/scu_constants.h>
#include <dev/isci/scil/scu_event_codes.h>
#include <dev/isci/scil/scu_completion_codes.h>
#include <dev/isci/scil/scu_task_context.h>
#include <dev/isci/scil/scu_remote_node_context.h>
#include <dev/isci/scil/scu_unsolicited_frame.h>
#include <dev/isci/scil/intel_pci.h>
#include <dev/isci/scil/scic_sgpio.h>
#include <dev/isci/scil/scic_sds_phy_registers.h>

#define SCU_CONTEXT_RAM_INIT_STALL_TIME      200
#define SCIC_SDS_CONTROLLER_MIN_TIMER_COUNT  3
#define SCIC_SDS_CONTROLLER_MAX_TIMER_COUNT  3

#define SCU_MAX_ZPT_DWORD_INDEX              131

/**
 * The number of milliseconds to wait for a phy to start.
 */
#define SCIC_SDS_CONTROLLER_PHY_START_TIMEOUT      100

/**
 * The number of milliseconds to wait while a given phy is consuming
 * power before allowing another set of phys to consume power.
 * Ultimately, this will be specified by OEM parameter.
 */
#define SCIC_SDS_CONTROLLER_POWER_CONTROL_INTERVAL 500

/**
 * This macro will return the cycle bit of the completion queue entry
 */
#define COMPLETION_QUEUE_CYCLE_BIT(x) ((x) & 0x80000000)

/**
 * This macro will normalize the completion queue get pointer so its value
 * can be used as an index into an array
 */
#define NORMALIZE_GET_POINTER(x) \
   ((x) & SMU_COMPLETION_QUEUE_GET_POINTER_MASK)

/**
 *  This macro will normalize the completion queue put pointer so its value
 *  can be used as an array inde
 */
#define NORMALIZE_PUT_POINTER(x) \
   ((x) & SMU_COMPLETION_QUEUE_PUT_POINTER_MASK)


/**
 * This macro will normalize the completion queue cycle pointer so it
 * matches the completion queue cycle bit
 */
#define NORMALIZE_GET_POINTER_CYCLE_BIT(x) \
   (((U32)(SMU_CQGR_CYCLE_BIT & (x))) << (31 - SMU_COMPLETION_QUEUE_GET_CYCLE_BIT_SHIFT))

/**
 * This macro will normalize the completion queue event entry so its value
 * can be used as an index.
 */
#define NORMALIZE_EVENT_POINTER(x) \
   ( \
        ((U32)((x) & SMU_COMPLETION_QUEUE_GET_EVENT_POINTER_MASK)) \
     >> SMU_COMPLETION_QUEUE_GET_EVENT_POINTER_SHIFT \
   )

/**
 * This macro will increment the controllers completion queue index value
 * and possibly toggle the cycle bit if the completion queue index wraps
 * back to 0.
 */
#define INCREMENT_COMPLETION_QUEUE_GET(controller, index, cycle) \
   INCREMENT_QUEUE_GET( \
      (index), \
      (cycle), \
      (controller)->completion_queue_entries, \
      SMU_CQGR_CYCLE_BIT \
   )

/**
 * This macro will increment the controllers event queue index value and
 * possibly toggle the event cycle bit if the event queue index wraps back
 * to 0.
 */
#define INCREMENT_EVENT_QUEUE_GET(controller, index, cycle) \
   INCREMENT_QUEUE_GET( \
      (index), \
      (cycle), \
      (controller)->completion_event_entries, \
      SMU_CQGR_EVENT_CYCLE_BIT \
   )

//****************************************************************************-
//* SCIC SDS Controller Initialization Methods
//****************************************************************************-

/**
 * @brief This timer is used to start another phy after we have given up on
 *        the previous phy to transition to the ready state.
 *
 * @param[in] controller
 */
static
void scic_sds_controller_phy_startup_timeout_handler(
   void *controller
)
{
   SCI_STATUS status;
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   this_controller->phy_startup_timer_pending = FALSE;

   status = SCI_FAILURE;

   while (status != SCI_SUCCESS)
   {
      status = scic_sds_controller_start_next_phy(this_controller);
   }
}

/**
 * This method initializes the phy startup operations for controller start.
 *
 * @param this_controller
 */
static
SCI_STATUS scic_sds_controller_initialize_phy_startup(
   SCIC_SDS_CONTROLLER_T *this_controller
)
{
   this_controller->phy_startup_timer = scic_cb_timer_create(
      this_controller,
      scic_sds_controller_phy_startup_timeout_handler,
      this_controller
   );

   if (this_controller->phy_startup_timer == NULL)
   {
      return SCI_FAILURE_INSUFFICIENT_RESOURCES;
   }
   else
   {
      this_controller->next_phy_to_start = 0;
      this_controller->phy_startup_timer_pending = FALSE;
   }

   return SCI_SUCCESS;
}

/**
 * This method initializes the power control operations for the controller
 * object.
 *
 * @param this_controller
 */
void scic_sds_controller_initialize_power_control(
   SCIC_SDS_CONTROLLER_T *this_controller
)
{
   this_controller->power_control.timer = scic_cb_timer_create(
      this_controller,
      scic_sds_controller_power_control_timer_handler,
      this_controller
   );

   memset(
      this_controller->power_control.requesters,
      0,
      sizeof(this_controller->power_control.requesters)
   );

   this_controller->power_control.phys_waiting = 0;
   this_controller->power_control.remote_devices_granted_power = 0;
}

// ---------------------------------------------------------------------------

#define SCU_REMOTE_NODE_CONTEXT_ALIGNMENT       (32)
#define SCU_TASK_CONTEXT_ALIGNMENT              (256)
#define SCU_UNSOLICITED_FRAME_ADDRESS_ALIGNMENT (64)
#define SCU_UNSOLICITED_FRAME_BUFFER_ALIGNMENT  (1024)
#define SCU_UNSOLICITED_FRAME_HEADER_ALIGNMENT  (64)

// ---------------------------------------------------------------------------

/**
 * @brief This method builds the memory descriptor table for this
 *        controller.
 *
 * @param[in] this_controller This parameter specifies the controller
 *            object for which to build the memory table.
 *
 * @return none
 */
void scic_sds_controller_build_memory_descriptor_table(
   SCIC_SDS_CONTROLLER_T *this_controller
)
{
   sci_base_mde_construct(
      &this_controller->memory_descriptors[SCU_MDE_COMPLETION_QUEUE],
      SCU_COMPLETION_RAM_ALIGNMENT,
      (sizeof(U32) * this_controller->completion_queue_entries),
      (SCI_MDE_ATTRIBUTE_CACHEABLE | SCI_MDE_ATTRIBUTE_PHYSICALLY_CONTIGUOUS)
   );

   sci_base_mde_construct(
      &this_controller->memory_descriptors[SCU_MDE_REMOTE_NODE_CONTEXT],
      SCU_REMOTE_NODE_CONTEXT_ALIGNMENT,
      this_controller->remote_node_entries * sizeof(SCU_REMOTE_NODE_CONTEXT_T),
      SCI_MDE_ATTRIBUTE_PHYSICALLY_CONTIGUOUS
   );

   sci_base_mde_construct(
      &this_controller->memory_descriptors[SCU_MDE_TASK_CONTEXT],
      SCU_TASK_CONTEXT_ALIGNMENT,
      this_controller->task_context_entries * sizeof(SCU_TASK_CONTEXT_T),
      SCI_MDE_ATTRIBUTE_PHYSICALLY_CONTIGUOUS
   );

   // The UF buffer address table size must be programmed to a power
   // of 2.  Find the first power of 2 that is equal to or greater then
   // the number of unsolicited frame buffers to be utilized.
   scic_sds_unsolicited_frame_control_set_address_table_count(
      &this_controller->uf_control
   );

   sci_base_mde_construct(
      &this_controller->memory_descriptors[SCU_MDE_UF_BUFFER],
      SCU_UNSOLICITED_FRAME_BUFFER_ALIGNMENT,
      scic_sds_unsolicited_frame_control_get_mde_size(this_controller->uf_control),
      SCI_MDE_ATTRIBUTE_PHYSICALLY_CONTIGUOUS
   );
}

/**
 * @brief This method validates the driver supplied memory descriptor
 *        table.
 *
 * @param[in] this_controller
 *
 * @return SCI_STATUS
 */
SCI_STATUS scic_sds_controller_validate_memory_descriptor_table(
   SCIC_SDS_CONTROLLER_T *this_controller
)
{
   BOOL mde_list_valid;

   mde_list_valid = sci_base_mde_is_valid(
      &this_controller->memory_descriptors[SCU_MDE_COMPLETION_QUEUE],
      SCU_COMPLETION_RAM_ALIGNMENT,
      (sizeof(U32) * this_controller->completion_queue_entries),
      (SCI_MDE_ATTRIBUTE_CACHEABLE | SCI_MDE_ATTRIBUTE_PHYSICALLY_CONTIGUOUS)
   );

   if (mde_list_valid == FALSE)
      return SCI_FAILURE_UNSUPPORTED_INFORMATION_FIELD;

   mde_list_valid = sci_base_mde_is_valid(
      &this_controller->memory_descriptors[SCU_MDE_REMOTE_NODE_CONTEXT],
      SCU_REMOTE_NODE_CONTEXT_ALIGNMENT,
      this_controller->remote_node_entries * sizeof(SCU_REMOTE_NODE_CONTEXT_T),
      SCI_MDE_ATTRIBUTE_PHYSICALLY_CONTIGUOUS
   );

   if (mde_list_valid == FALSE)
      return SCI_FAILURE_UNSUPPORTED_INFORMATION_FIELD;

   mde_list_valid = sci_base_mde_is_valid(
      &this_controller->memory_descriptors[SCU_MDE_TASK_CONTEXT],
      SCU_TASK_CONTEXT_ALIGNMENT,
      this_controller->task_context_entries * sizeof(SCU_TASK_CONTEXT_T),
      SCI_MDE_ATTRIBUTE_PHYSICALLY_CONTIGUOUS
   );

   if (mde_list_valid == FALSE)
      return SCI_FAILURE_UNSUPPORTED_INFORMATION_FIELD;

   mde_list_valid = sci_base_mde_is_valid(
      &this_controller->memory_descriptors[SCU_MDE_UF_BUFFER],
      SCU_UNSOLICITED_FRAME_BUFFER_ALIGNMENT,
      scic_sds_unsolicited_frame_control_get_mde_size(this_controller->uf_control),
      SCI_MDE_ATTRIBUTE_PHYSICALLY_CONTIGUOUS
   );

   if (mde_list_valid == FALSE)
      return SCI_FAILURE_UNSUPPORTED_INFORMATION_FIELD;

   return SCI_SUCCESS;
}

/**
 * @brief This method initializes the controller with the physical memory
 *        addresses that are used to communicate with the driver.
 *
 * @param[in] this_controller
 *
 * @return none
 */
void scic_sds_controller_ram_initialization(
   SCIC_SDS_CONTROLLER_T *this_controller
)
{
   SCI_PHYSICAL_MEMORY_DESCRIPTOR_T *mde;

   // The completion queue is actually placed in cacheable memory
   // Therefore it no longer comes out of memory in the MDL.
   mde = &this_controller->memory_descriptors[SCU_MDE_COMPLETION_QUEUE];
   this_controller->completion_queue = (U32*) mde->virtual_address;
   SMU_CQBAR_WRITE(this_controller, mde->physical_address);

   // Program the location of the Remote Node Context table
   // into the SCU.
   mde = &this_controller->memory_descriptors[SCU_MDE_REMOTE_NODE_CONTEXT];
   this_controller->remote_node_context_table = (SCU_REMOTE_NODE_CONTEXT_T *)
                                                mde->virtual_address;
   SMU_RNCBAR_WRITE(this_controller, mde->physical_address);

   // Program the location of the Task Context table into the SCU.
   mde = &this_controller->memory_descriptors[SCU_MDE_TASK_CONTEXT];
   this_controller->task_context_table = (SCU_TASK_CONTEXT_T *)
                                         mde->virtual_address;
   SMU_HTTBAR_WRITE(this_controller, mde->physical_address);

   mde = &this_controller->memory_descriptors[SCU_MDE_UF_BUFFER];
   scic_sds_unsolicited_frame_control_construct(
      &this_controller->uf_control, mde, this_controller
   );

   // Inform the silicon as to the location of the UF headers and
   // address table.
   SCU_UFHBAR_WRITE(
      this_controller,
      this_controller->uf_control.headers.physical_address);
   SCU_PUFATHAR_WRITE(
      this_controller,
      this_controller->uf_control.address_table.physical_address);

   //enable the ECC correction and detection.
   SCU_SECR0_WRITE(
      this_controller,
      (SIGNLE_BIT_ERROR_CORRECTION_ENABLE
       | MULTI_BIT_ERROR_REPORTING_ENABLE
       | SINGLE_BIT_ERROR_REPORTING_ENABLE) );
   SCU_SECR1_WRITE(
      this_controller,
      (SIGNLE_BIT_ERROR_CORRECTION_ENABLE
       | MULTI_BIT_ERROR_REPORTING_ENABLE
       | SINGLE_BIT_ERROR_REPORTING_ENABLE) );
}

/**
 * @brief This method initializes the task context data for the controller.
 *
 * @param[in] this_controller
 *
 * @return none
 */
void scic_sds_controller_assign_task_entries(
   SCIC_SDS_CONTROLLER_T *this_controller
)
{
   U32 task_assignment;

   // Assign all the TCs to function 0
   // TODO: Do we actually need to read this register to write it back?
   task_assignment = SMU_TCA_READ(this_controller, 0);

   task_assignment =
      (
          task_assignment
        | (SMU_TCA_GEN_VAL(STARTING, 0))
        | (SMU_TCA_GEN_VAL(ENDING,  this_controller->task_context_entries - 1))
        | (SMU_TCA_GEN_BIT(RANGE_CHECK_ENABLE))
      );

   SMU_TCA_WRITE(this_controller, 0, task_assignment);
}

/**
 * @brief This method initializes the hardware completion queue.
 *
 * @param[in] this_controller
 */
void scic_sds_controller_initialize_completion_queue(
   SCIC_SDS_CONTROLLER_T *this_controller
)
{
   U32 index;
   U32 completion_queue_control_value;
   U32 completion_queue_get_value;
   U32 completion_queue_put_value;

   this_controller->completion_queue_get = 0;

   completion_queue_control_value = (
        SMU_CQC_QUEUE_LIMIT_SET(this_controller->completion_queue_entries - 1)
      | SMU_CQC_EVENT_LIMIT_SET(this_controller->completion_event_entries - 1)
   );

   SMU_CQC_WRITE(this_controller, completion_queue_control_value);

   // Set the completion queue get pointer and enable the queue
   completion_queue_get_value = (
        (SMU_CQGR_GEN_VAL(POINTER, 0))
      | (SMU_CQGR_GEN_VAL(EVENT_POINTER, 0))
      | (SMU_CQGR_GEN_BIT(ENABLE))
      | (SMU_CQGR_GEN_BIT(EVENT_ENABLE))
   );

   SMU_CQGR_WRITE(this_controller, completion_queue_get_value);

   this_controller->completion_queue_get = completion_queue_get_value;

   // Set the completion queue put pointer
   completion_queue_put_value = (
        (SMU_CQPR_GEN_VAL(POINTER, 0))
      | (SMU_CQPR_GEN_VAL(EVENT_POINTER, 0))
   );

   SMU_CQPR_WRITE(this_controller, completion_queue_put_value);

   // Initialize the cycle bit of the completion queue entries
   for (index = 0; index < this_controller->completion_queue_entries; index++)
   {
      // If get.cycle_bit != completion_queue.cycle_bit
      // its not a valid completion queue entry
      // so at system start all entries are invalid
      this_controller->completion_queue[index] = 0x80000000;
   }
}

/**
 * @brief This method initializes the hardware unsolicited frame queue.
 *
 * @param[in] this_controller
 */
void scic_sds_controller_initialize_unsolicited_frame_queue(
   SCIC_SDS_CONTROLLER_T *this_controller
)
{
   U32 frame_queue_control_value;
   U32 frame_queue_get_value;
   U32 frame_queue_put_value;

   // Write the queue size
   frame_queue_control_value =
      SCU_UFQC_GEN_VAL(QUEUE_SIZE, this_controller->uf_control.address_table.count);

   SCU_UFQC_WRITE(this_controller, frame_queue_control_value);

   // Setup the get pointer for the unsolicited frame queue
   frame_queue_get_value = (
         SCU_UFQGP_GEN_VAL(POINTER, 0)
      |  SCU_UFQGP_GEN_BIT(ENABLE_BIT)
      );

   SCU_UFQGP_WRITE(this_controller, frame_queue_get_value);

   // Setup the put pointer for the unsolicited frame queue
   frame_queue_put_value = SCU_UFQPP_GEN_VAL(POINTER, 0);

   SCU_UFQPP_WRITE(this_controller, frame_queue_put_value);
}

/**
 * @brief This method enables the hardware port task scheduler.
 *
 * @param[in] this_controller
 */
void scic_sds_controller_enable_port_task_scheduler(
   SCIC_SDS_CONTROLLER_T *this_controller
)
{
   U32 port_task_scheduler_value;

   port_task_scheduler_value = SCU_PTSGCR_READ(this_controller);

   port_task_scheduler_value |=
      (SCU_PTSGCR_GEN_BIT(ETM_ENABLE) | SCU_PTSGCR_GEN_BIT(PTSG_ENABLE));

   SCU_PTSGCR_WRITE(this_controller, port_task_scheduler_value);
}

// ---------------------------------------------------------------------------

#ifdef ARLINGTON_BUILD
/**
 * This method will read from the lexington status register.  This is required
 * as a read fence to the lexington register writes.
 *
 * @param this_controller
 */
void scic_sds_controller_lex_status_read_fence(
   SCIC_SDS_CONTROLLER_T *this_controller
)
{
   U32 lex_status;

   // Read Fence
   lex_status = lex_register_read(
                  this_controller, this_controller->lex_registers + 0xC4);

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_controller),
      SCIC_LOG_OBJECT_CONTROLLER,
      "Controller 0x%x lex_status = 0x%08x\n",
      this_controller, lex_status
   ));
}

/**
 * This method will initialize the arlington through the LEX_BAR.
 *
 * @param this_controller
 */
void scic_sds_controller_lex_atux_initialization(
   SCIC_SDS_CONTROLLER_T *this_controller
)
{
   // 1. Reset all SCU PHY
   lex_register_write(
      this_controller, this_controller->lex_registers + 0x28, 0x0020FFFF) ;

   // 2. Write to LEX_CTRL
   lex_register_write(
      this_controller, this_controller->lex_registers + 0xC0, 0x00000700);

   scic_sds_controller_lex_status_read_fence(this_controller);

   // 3. Enable PCI Master
   lex_register_write(
      this_controller, this_controller->lex_registers + 0x70, 0x00000002);

   // 4. Enable SCU Register Clock Domain
   lex_register_write(
      this_controller, this_controller->lex_registers + 0xC0, 0x00000300);

   scic_sds_controller_lex_status_read_fence(this_controller);

   // 5.1 Release PHY-A Reg Reset
   lex_register_write(
      this_controller, this_controller->lex_registers + 0x28, 0x0000FFFF);

   // 5.2 Initialize the AFE for PHY-A
   scic_sds_controller_afe_initialization(this_controller);

   scic_sds_controller_lex_status_read_fence(this_controller);

#if 0
   // 5.3 Release PHY Reg Reset
   lex_register_write(
      this_controller, this_controller->lex_registers + 0x28, 0x0000FFFF);
#endif

   // 6.1 Release PHY-B Reg Reset
   lex_register_write(
      this_controller, this_controller->lex_registers + 0x28, 0x0040FFFF) ;

   // 6.2 Initialize the AFE for PHY-B
   scic_sds_controller_afe_initialization(this_controller);

   scic_sds_controller_lex_status_read_fence(this_controller);

#if 0
   // 6.3 Release PHY-B Reg Reset
   lex_register_write(
      this_controller, this_controller->lex_registers + 0x28, 0x0040FFFF) ;
#endif

   // 7. Enable SCU clock domaion
   lex_register_write(
      this_controller, this_controller->lex_registers + 0xC0, 0x00000100);

   scic_sds_controller_lex_status_read_fence(this_controller);

   // 8. Release LEX SCU Reset
   lex_register_write(
      this_controller, this_controller->lex_registers + 0xC0, 0x00000000);

   scic_sds_controller_lex_status_read_fence(this_controller);

#if !defined(DISABLE_INTERRUPTS)
   // 8a. Set legacy interrupts (SCU INTx to PCI-x INTA)
   lex_register_write(
      this_controller, this_controller->lex_registers + 0xC0, 0x00000800);

   scic_sds_controller_lex_status_read_fence(this_controller);
#endif

#if 0
   // 9. Override TXOLVL
   //write to lex_ctrl
   lex_register_write(
      this_controller, this_controller->lex_registers + 0xC0, 0x27800000);
#endif

   // 10. Release PHY-A & PHY-B Resets
   lex_register_write(
      this_controller, this_controller->lex_registers + 0x28, 0x0000FF77);

   lex_register_write(
      this_controller, this_controller->lex_registers + 0x28, 0x0000FF55);

   lex_register_write(
      this_controller, this_controller->lex_registers + 0x28, 0x0000FF11);

   lex_register_write(
      this_controller, this_controller->lex_registers + 0x28, 0x0000FF00);

   lex_register_write(
      this_controller, this_controller->lex_registers + 0x28, 0x0003FF00);
}
#endif // ARLINGTON_BUILD

// ---------------------------------------------------------------------------

#ifdef ARLINGTON_BUILD
/**
 * This method enables chipwatch on the arlington board
 *
 * @param[in] this_controller
 */
void scic_sds_controller_enable_chipwatch(
   SCIC_SDS_CONTROLLER_T *this_controller
)
{
   lex_register_write(
      this_controller, this_controller->lex_registers + 0x88, 0x09090909);

   lex_register_write(
      this_controller, this_controller->lex_registers + 0x8C, 0xcac9c862);
}
#endif

/**
 * This macro is used to delay between writes to the AFE registers
 * during AFE initialization.
 */
#define AFE_REGISTER_WRITE_DELAY 10

/**
 * Initialize the AFE for this phy index.
 *
 * @todo We need to read the AFE setup from the OEM parameters
 *
 * @param[in] this_controller
 *
 * @return none
 */
#if defined(ARLINGTON_BUILD)
void scic_sds_controller_afe_initialization(
   SCIC_SDS_CONTROLLER_T *this_controller
)
{
   // 1. Establish Power
   //       Hold Bias, PLL, and RX TX in reset and powerdown
   //       pe_afe0_rst_n = 0
   //       pe_afe0_txpdn0,1,2,3 = 1
   //       pe_afe0_rxpdn0,1,2,3 = 1
   //       pe_afe0_txrst0,1,2,3_n = 0
   //       pe_afe0_rxrst0,1,2,3_n = 0
   //       wait 1us
   //       pe_afe0_rst_n = 1
   //       wait 1us
   scu_afe_register_write(
      this_controller, afe_pll_control, 0x00247506);

   // 2. Write 0x00000000 to AFE XCVR Ctrl2
   scu_afe_register_write(
      this_controller, afe_dfx_transceiver_status_clear, 0x00000000);

   // 3. afe0_override_en = 0
   //    afe0_pll_dis_override = 0
   //    afe0_tx_rst_override = 0
   //    afe0_pll_dis = 1
   //    pe_afe0_txrate = 01
   //    pe_afe0_rxrate = 01
   //    pe_afe0_txdis = 11
   //    pe_afe0_txoob = 1
   //    pe_afe0_txovlv = 9'b001110000
   scu_afe_register_write(
      this_controller, afe_transceiver_control0[0], 0x0700141e);

   // 4. Configure PLL Unit
   //    Write 0x00200506 to AFE PLL Ctrl Register 0
   scu_afe_register_write(this_controller, afe_pll_control,     0x00200506);
   scu_afe_register_write(this_controller, afe_pll_dfx_control, 0x10000080);

   // 5. Configure Bias Unit
   scu_afe_register_write(this_controller, afe_bias_control[0], 0x00124814);
   scu_afe_register_write(this_controller, afe_bias_control[1], 0x24900000);

   // 6. Configure Transceiver Units
   scu_afe_register_write(
      this_controller, afe_transceiver_control0[0], 0x0702941e);

   scu_afe_register_write(
      this_controller, afe_transceiver_control1[0], 0x0000000a);

   // 7. Configure RX Units
   scu_afe_register_write(
      this_controller, afe_transceiver_equalization_control[0], 0x00ba2223);

   scu_afe_register_write(
      this_controller, reserved_0028_003c[2], 0x00000000);

   // 8. Configure TX Units
   scu_afe_register_write(
      this_controller, afe_dfx_transmit_control_register[0], 0x03815428);

   // 9. Transfer control to PE signals
   scu_afe_register_write(
      this_controller, afe_dfx_transceiver_status_clear, 0x00000010);

   // 10. Release PLL Powerdown
   scu_afe_register_write(this_controller, afe_pll_control, 0x00200504);

   // 11. Release PLL Reset
   scu_afe_register_write(this_controller, afe_pll_control, 0x00200505);

   // 12. Wait for PLL to Lock
   // (afe0_comm_sta [1:0] should go to 1'b11, and
   //                [5:2] is 0x5, 0x6, 0x7, 0x8, or 0x9
   scu_afe_register_write(this_controller, afe_pll_control, 0x00200501);

   while ((scu_afe_register_read(this_controller, afe_common_status) & 0x03) != 0x03)
   {
      // Give time for the PLLs to lock
      scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);
   }

   // 13. pe_afe0_rxpdn0 = 0
   //     pe_afe0_rxrst0 = 1
   //     pe_afe0_txrst0_n = 1
   //     pe_afe_txoob0_n = 0
   scu_afe_register_write(
      this_controller, afe_transceiver_control0[0], 0x07028c11);
}

#elif defined(PLEASANT_RIDGE_BUILD)

void scic_sds_controller_afe_initialization(
   SCIC_SDS_CONTROLLER_T *this_controller
)
{
   U32 afe_status;
   U32 phy_id;

#if defined(SPREADSHEET_AFE_SETTINGS)
   // Clear DFX Status registers
   scu_afe_register_write(
      this_controller, afe_dfx_master_control0, 0x0000000f);
   // Configure bias currents to normal
   scu_afe_register_write(
      this_controller, afe_bias_control, 0x0000aa00);
   // Enable PLL
   scu_afe_register_write(
      this_controller, afe_pll_control0, 0x80000908);

   // Wait for the PLL to lock
   do
   {
      scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);
      afe_status = scu_afe_register_read(
                     this_controller, afe_common_block_status);
   }
   while((afe_status & 0x00001000) == 0);

   for (phy_id = 0; phy_id < SCI_MAX_PHYS; phy_id++)
   {
      // Initialize transceiver channels
      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_channel_control, 0x00000157);
      // Configure transceiver modes
      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_xcvr_control0, 0x38016d1a);
      // Configure receiver parameters
      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_xcvr_control1, 0x01501014);
      // Configure transmitter parameters
      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_tx_control, 0x00000000);
      // Configure transmitter equalization
      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_tx_amp_control0, 0x000bdd08);
      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_tx_amp_control1, 0x000ffc00);
      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_tx_amp_control2, 0x000b7c09);
      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_tx_amp_control3, 0x000afc6e);
      // Configure transmitter SSC parameters
      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_tx_ssc_control, 0x00000000);
      // Configure receiver parameters
      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_rx_ssc_control0, 0x3208903f);

      // Start power on sequence
      // Enable bias currents to transceivers and wait 200ns
      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_channel_control, 0x00000154);
      scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);
      // Take receiver out of power down and wait 200ns
      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_xcvr_control0, 0x3801611a);
      scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);
      // Take receiver out of reset and wait 200ns
      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_xcvr_control0, 0x3801631a);
      scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);
      // Take transmitter out of power down and wait 200ns
      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_xcvr_control0, 0x38016318);
      scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);
      // Take transmitter out of reset and wait 200ns
      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_xcvr_control0, 0x38016319);
      scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);
      // Take transmitter out of DC idle
      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_xcvr_control0, 0x38016319);
      scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);
   }

   // Transfer control to the PEs
   scu_afe_register_write(
      this_controller, afe_dfx_master_control0, 0x00010f00);
#else // !defined(SPREADSHEET_AFE_SETTINGS)
   // These are the AFEE settings used by the SV group
   // Clear DFX Status registers
   scu_afe_register_write(
      this_controller, afe_dfx_master_control0, 0x0081000f);
   // Configure bias currents to normal
   scu_afe_register_write(
      this_controller, afe_bias_control, 0x0000aa00);
   // Enable PLL
   scu_afe_register_write(
      this_controller, afe_pll_control0, 0x80000908);

   // Wait for the PLL to lock
   // Note: this is done later in the SV shell script however this looks
   //       like the location to do this since we have enabled the PLL.
   do
   {
      scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);
      afe_status = scu_afe_register_read(
                     this_controller, afe_common_block_status);
   }
   while((afe_status & 0x00001000) == 0);

   // Make sure BIST is disabled
   scu_afe_register_write(
      this_controller, afe_dfx_master_control1, 0x00000000);
   // Shorten SAS SNW lock time
   scu_afe_register_write(
      this_controller, afe_pmsn_master_control0, 0x7bd316ad);

   for (phy_id = 0; phy_id < SCI_MAX_PHYS; phy_id++)
   {
      // Initialize transceiver channels
      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_channel_control, 0x00000174);
      // Configure SSC control
      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_tx_ssc_control, 0x00030000);
      // Configure transceiver modes
      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_xcvr_control0, 0x0000651a);
      // Power up TX RX and RX OOB
      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_xcvr_control0, 0x00006518);
      // Enable RX OOB Detect
      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_xcvr_control0, 0x00006518);
      scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);
      #if 0
      // Configure transmitter parameters
      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_tx_control, 0x00000000);
      // Configure transmitter equalization
      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_tx_amp_control0, 0x000bdd08);
      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_tx_amp_control1, 0x000ffc00);
      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_tx_amp_control2, 0x000b7c09);
      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_tx_amp_control3, 0x000afc6e);
      // Configure transmitter SSC parameters
      // Power up TX RX

      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_channel_control, 0x00000154);
      scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);

      // FFE = Max
      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_dfx_rx_control1, 0x00000080);
      // DFE1-5 = small
      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_dfx_rx_control1, 0x01041042);
      // Enable DFE/FFE and freeze
      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_rx_ssc_control0, 0x320891bf);
      #endif
      // Take receiver out of power down and wait 200ns
      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_xcvr_control0, 0x00006118);
      scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);
      // TX Electrical Idle
      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_xcvr_control0, 0x00006108);
      scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);

      // Leave DFE/FFE on
      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_rx_ssc_control0, 0x0317108f);

      // Configure receiver parameters
      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_xcvr_control1, 0x01e00021);

      // Bring RX out of reset
      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_xcvr_control0, 0x00006109);
      scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);

      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_xcvr_control0, 0x00006009);
      scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);

      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_xcvr_control0, 0x00006209);
      scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);
   }

   // Transfer control to the PEs
   scu_afe_register_write(
      this_controller, afe_dfx_master_control0, 0x00010f00);
#endif
}

#elif defined(PBG_HBA_A0_BUILD) || defined(PBG_HBA_A2_BUILD) || defined(PBG_HBA_BETA_BUILD) || defined(PBG_BUILD)

void scic_sds_controller_afe_initialization(
   SCIC_SDS_CONTROLLER_T *this_controller
)
{
   U32  afe_status;
   U32  phy_id;
   U8   cable_selection_mask;

   if (
         (this_controller->pci_revision != SCIC_SDS_PCI_REVISION_A0)
      && (this_controller->pci_revision != SCIC_SDS_PCI_REVISION_A2)
      && (this_controller->pci_revision != SCIC_SDS_PCI_REVISION_B0)
      && (this_controller->pci_revision != SCIC_SDS_PCI_REVISION_C0)
      && (this_controller->pci_revision != SCIC_SDS_PCI_REVISION_C1)
      )
   {
      // A programming bug has occurred if we are attempting to
      // support a PCI revision other than those listed.  Default
      // to B0, and attempt to limp along if it isn't B0.
      ASSERT(FALSE);
      this_controller->pci_revision = SCIC_SDS_PCI_REVISION_C1;
   }

   cable_selection_mask =
      this_controller->oem_parameters.sds1.controller.cable_selection_mask;

   // These are the AFEE settings used by the SV group
   // Clear DFX Status registers
   scu_afe_register_write(
      this_controller, afe_dfx_master_control0, 0x0081000f);
   scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);

   if (
         (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_B0)
      || (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_C0)
      || (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_C1)
      )
   {
      // PM Rx Equalization Save, PM SPhy Rx Acknowledgement Timer, PM Stagger Timer
      scu_afe_register_write(
         this_controller, afe_pmsn_master_control2, 0x0007FFFF);
      scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);
   }

   // Configure bias currents to normal
   if (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_A0)
      scu_afe_register_write(this_controller, afe_bias_control, 0x00005500);
   else if (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_A2)
      scu_afe_register_write(this_controller, afe_bias_control, 0x00005A00);
   else if (  (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_B0)
           || (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_C0) )
      scu_afe_register_write(this_controller, afe_bias_control, 0x00005F00);
   else if (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_C1)
      scu_afe_register_write(this_controller, afe_bias_control, 0x00005500);
   // For C0 the AFE BIAS Control is unchanged

   scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);

      // Enable PLL
   if (  (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_A0)
      || (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_A2) )
   {
      scu_afe_register_write(this_controller, afe_pll_control0, 0x80040908);
   }
   else if (  (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_B0)
           || (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_C0) )
   {
      scu_afe_register_write(this_controller, afe_pll_control0, 0x80040A08);
   }
   else if (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_C1)
   {
      scu_afe_register_write(this_controller, afe_pll_control0, 0x80000b08);
      scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);
      scu_afe_register_write(this_controller, afe_pll_control0, 0x00000b08);
      scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);
      scu_afe_register_write(this_controller, afe_pll_control0, 0x80000b08);
   }

   scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);

   // Wait for the PLL to lock
   // Note: this is done later in the SV shell script however this looks
   //       like the location to do this since we have enabled the PLL.
   do
   {
      afe_status = scu_afe_register_read(
                      this_controller, afe_common_block_status);
      scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);
   }
   while((afe_status & 0x00001000) == 0);

   if (  (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_A0)
      || (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_A2) )
   {
      // Shorten SAS SNW lock time (RxLock timer value from 76 us to 50 us)
      scu_afe_register_write(
         this_controller, afe_pmsn_master_control0, 0x7bcc96ad);
      scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);
   }

   for (phy_id = 0; phy_id < SCI_MAX_PHYS; phy_id++)
   {
      U8 cable_length_long   = (cable_selection_mask >> phy_id) & 1;
      U8 cable_length_medium = (cable_selection_mask >> (phy_id + 4)) & 1;

      if (  (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_A0)
         || (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_A2) )
      {
         // All defaults, except the Receive Word Alignament/Comma Detect
         // Enable....(0xe800)
         scu_afe_register_write(
            this_controller, scu_afe_xcvr[phy_id].afe_xcvr_control0, 0x00004512
         );
         scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);

         scu_afe_register_write(
            this_controller, scu_afe_xcvr[phy_id].afe_xcvr_control1, 0x0050100F
         );
         scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);
      }
      else if (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_B0)
      {
         // Configure transmitter SSC parameters
         scu_afe_register_write(
            this_controller, scu_afe_xcvr[phy_id].afe_tx_ssc_control, 0x00030000
         );
         scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);
      }
      else if (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_C0)
      {
         // Configure transmitter SSC parameters
         scu_afe_register_write(
            this_controller, scu_afe_xcvr[phy_id].afe_tx_ssc_control, 0x00010202
         );
         scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);

         // All defaults, except the Receive Word Alignament/Comma Detect
         // Enable....(0xe800)
         scu_afe_register_write(
            this_controller, scu_afe_xcvr[phy_id].afe_xcvr_control0, 0x00014500
         );
         scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);
      }
      else if (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_C1)
      {
         // Configure transmitter SSC parameters
         scu_afe_register_write(
            this_controller, scu_afe_xcvr[phy_id].afe_tx_ssc_control, 0x00010202
         );
         scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);

         // All defaults, except the Receive Word Alignament/Comma Detect
         // Enable....(0xe800)
         scu_afe_register_write(
            this_controller, scu_afe_xcvr[phy_id].afe_xcvr_control0, 0x0001C500
         );
         scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);
      }
      // Power up TX and RX out from power down (PWRDNTX and PWRDNRX)
      // & increase TX int & ext bias 20%....(0xe85c)
      if (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_A0)
      {
         scu_afe_register_write(
            this_controller,
            scu_afe_xcvr[phy_id].afe_channel_control,
            0x000003D4
         );
      }
      else if (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_A2)
      {
         scu_afe_register_write(
            this_controller,
            scu_afe_xcvr[phy_id].afe_channel_control,
            0x000003F0
         );
      }
      else if (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_B0)
      {
         // Power down TX and RX (PWRDNTX and PWRDNRX)
         scu_afe_register_write(
            this_controller,
            scu_afe_xcvr[phy_id].afe_channel_control,
            0x000003d7
         );

         scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);

         // Power up TX and RX out from power down (PWRDNTX and PWRDNRX)
         // & increase TX int & ext bias 20%....(0xe85c)
         scu_afe_register_write(
            this_controller,
            scu_afe_xcvr[phy_id].afe_channel_control,
            0x000003d4
         );
      }
      else if (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_C0)
      {
         scu_afe_register_write(
            this_controller,
            scu_afe_xcvr[phy_id].afe_channel_control,
            0x000001e7
         );

         scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);

         // Power up TX and RX out from power down (PWRDNTX and PWRDNRX)
         // & increase TX int & ext bias 20%....(0xe85c)
         scu_afe_register_write(
            this_controller,
            scu_afe_xcvr[phy_id].afe_channel_control,
            0x000001e4
         );
      }
      else if (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_C1)
      {
         scu_afe_register_write(
            this_controller,
            scu_afe_xcvr[phy_id].afe_channel_control,
            cable_length_long   ? 0x000002F7 :
            cable_length_medium ? 0x000001F7 : 0x000001F7
         );

         scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);

         // Power up TX and RX out from power down (PWRDNTX and PWRDNRX)
         // & increase TX int & ext bias 20%....(0xe85c)
         scu_afe_register_write(
            this_controller,
            scu_afe_xcvr[phy_id].afe_channel_control,
            cable_length_long   ? 0x000002F4 :
            cable_length_medium ? 0x000001F4 : 0x000001F4
         );
      }

      scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);

      if (  (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_A0)
         || (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_A2) )
      {
         // Enable TX equalization (0xe824)
         scu_afe_register_write(
            this_controller,
            scu_afe_xcvr[phy_id].afe_tx_control,
            0x00040000
         );
         scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);
      }

      if (  (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_A0)
         || (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_A2)
         || (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_B0) )
      {
         // RDPI=0x0(RX Power On), RXOOBDETPDNC=0x0, TPD=0x0(TX Power On),
         // RDD=0x0(RX Detect Enabled) ....(0xe800)
         scu_afe_register_write(
            this_controller, scu_afe_xcvr[phy_id].afe_xcvr_control0, 0x00004100);
         scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);
      }
      else if (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_C0)
      {
         scu_afe_register_write(
            this_controller, scu_afe_xcvr[phy_id].afe_xcvr_control0, 0x00014100);
         scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);
      }
      else if (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_C1)
      {
         scu_afe_register_write(
            this_controller, scu_afe_xcvr[phy_id].afe_xcvr_control0, 0x0001c100);
         scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);
      }

      // Leave DFE/FFE on
      if (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_A0)
      {
         scu_afe_register_write(
            this_controller,
            scu_afe_xcvr[phy_id].afe_rx_ssc_control0,
            0x3F09983F
         );
      }
      else if (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_A2)
      {
         scu_afe_register_write(
            this_controller,
            scu_afe_xcvr[phy_id].afe_rx_ssc_control0,
            0x3F11103F
         );
      }
      else if (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_B0)
      {
         scu_afe_register_write(
            this_controller,
            scu_afe_xcvr[phy_id].afe_rx_ssc_control0,
            0x3F11103F
         );
         scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);

         // Enable TX equalization (0xe824)
         scu_afe_register_write(
            this_controller, scu_afe_xcvr[phy_id].afe_tx_control, 0x00040000);
      }
      else if (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_C0)
      {
         scu_afe_register_write(
            this_controller, scu_afe_xcvr[phy_id].afe_xcvr_control1, 0x01400c0f);
         scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);

         scu_afe_register_write(
            this_controller, scu_afe_xcvr[phy_id].afe_rx_ssc_control0, 0x3f6f103f);
         scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);

         // Enable TX equalization (0xe824)
         scu_afe_register_write(
            this_controller, scu_afe_xcvr[phy_id].afe_tx_control, 0x00040000);
      }
      else if (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_C1)
      {
         scu_afe_register_write(
            this_controller, scu_afe_xcvr[phy_id].afe_xcvr_control1,
            cable_length_long   ? 0x01500C0C :
            cable_length_medium ? 0x01400C0D : 0x02400C0D
         );
         scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);

         scu_afe_register_write(
            this_controller, scu_afe_xcvr[phy_id].afe_dfx_rx_control1, 0x000003e0);
         scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);

         scu_afe_register_write(
            this_controller, scu_afe_xcvr[phy_id].afe_rx_ssc_control0,
            cable_length_long   ? 0x33091C1F :
            cable_length_medium ? 0x3315181F : 0x2B17161F
         );
         scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);

         // Enable TX equalization (0xe824)
         scu_afe_register_write(
            this_controller, scu_afe_xcvr[phy_id].afe_tx_control, 0x00040000);
      }

      scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);

      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_tx_amp_control0,
         this_controller->oem_parameters.sds1.phys[phy_id].afe_tx_amp_control0
      );
      scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);

      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_tx_amp_control1,
         this_controller->oem_parameters.sds1.phys[phy_id].afe_tx_amp_control1
      );
      scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);

      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_tx_amp_control2,
         this_controller->oem_parameters.sds1.phys[phy_id].afe_tx_amp_control2
      );
      scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);

      scu_afe_register_write(
         this_controller, scu_afe_xcvr[phy_id].afe_tx_amp_control3,
         this_controller->oem_parameters.sds1.phys[phy_id].afe_tx_amp_control3
      );
      scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);
   }

   // Transfer control to the PEs
   scu_afe_register_write(
      this_controller, afe_dfx_master_control0, 0x00010f00);
   scic_cb_stall_execution(AFE_REGISTER_WRITE_DELAY);
}
#else
   #error "Unsupported board type"
#endif

//****************************************************************************-
//* SCIC SDS Controller Internal Start/Stop Routines
//****************************************************************************-


/**
 * @brief This method will attempt to transition into the ready state
 *        for the controller and indicate that the controller start
 *        operation has completed if all criteria are met.
 *
 * @param[in,out] this_controller This parameter indicates the controller
 *                object for which to transition to ready.
 * @param[in]     status This parameter indicates the status value to be
 *                pass into the call to scic_cb_controller_start_complete().
 *
 * @return none.
 */
static
void scic_sds_controller_transition_to_ready(
   SCIC_SDS_CONTROLLER_T *this_controller,
   SCI_STATUS             status
)
{
   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_controller),
      SCIC_LOG_OBJECT_CONTROLLER,
      "scic_sds_controller_transition_to_ready(0x%x, 0x%x) enter\n",
      this_controller, status
   ));

   if (this_controller->parent.state_machine.current_state_id
       == SCI_BASE_CONTROLLER_STATE_STARTING)
   {
      // We move into the ready state, because some of the phys/ports
      // may be up and operational.
      sci_base_state_machine_change_state(
         scic_sds_controller_get_base_state_machine(this_controller),
         SCI_BASE_CONTROLLER_STATE_READY
      );

      scic_cb_controller_start_complete(this_controller, status);
   }
}

/**
 * @brief This method is the general timeout handler for the controller.
 *        It will take the correct timetout action based on the current
 *        controller state
 *
 * @param[in] controller This parameter indicates the controller on which
 *            a timeout occurred.
 *
 * @return none
 */
void scic_sds_controller_timeout_handler(
   SCI_CONTROLLER_HANDLE_T controller
)
{
   SCI_BASE_CONTROLLER_STATES current_state;
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   current_state = sci_base_state_machine_get_state(
                      scic_sds_controller_get_base_state_machine(this_controller)
                   );

   if (current_state == SCI_BASE_CONTROLLER_STATE_STARTING)
   {
      scic_sds_controller_transition_to_ready(
         this_controller, SCI_FAILURE_TIMEOUT
      );
   }
   else if (current_state == SCI_BASE_CONTROLLER_STATE_STOPPING)
   {
      sci_base_state_machine_change_state(
         scic_sds_controller_get_base_state_machine(this_controller),
         SCI_BASE_CONTROLLER_STATE_FAILED
      );

      scic_cb_controller_stop_complete(controller, SCI_FAILURE_TIMEOUT);
   }
   else
   {
      /// @todo Now what do we want to do in this case?
      SCIC_LOG_ERROR((
         sci_base_object_get_logger(this_controller),
         SCIC_LOG_OBJECT_CONTROLLER,
         "Controller timer fired when controller was not in a state being timed.\n"
      ));
   }
}

/**
 * @brief
 *
 * @param[in] this_controller
 *
 * @return SCI_STATUS
 */
SCI_STATUS scic_sds_controller_stop_ports(
   SCIC_SDS_CONTROLLER_T *this_controller
)
{
   U32        index;
   SCI_STATUS status;
   SCI_STATUS port_status;

   status = SCI_SUCCESS;

   for (index = 0; index < this_controller->logical_port_entries; index++)
   {
      port_status = this_controller->port_table[index].
         state_handlers->parent.stop_handler(&this_controller->port_table[index].parent);
      if (
            (port_status != SCI_SUCCESS)
         && (port_status != SCI_FAILURE_INVALID_STATE)
         )
      {
         status = SCI_FAILURE;

         SCIC_LOG_WARNING((
            sci_base_object_get_logger(this_controller),
            SCIC_LOG_OBJECT_CONTROLLER | SCIC_LOG_OBJECT_PORT,
            "Controller stop operation failed to stop port %d because of status %d.\n",
            this_controller->port_table[index].logical_port_index, port_status
         ));
      }
   }

   return status;
}

/**
 * @brief
 *
 * @param[in] this_controller
 */
static
void scic_sds_controller_phy_timer_start(
   SCIC_SDS_CONTROLLER_T *this_controller
)
{
   scic_cb_timer_start(
      this_controller,
      this_controller->phy_startup_timer,
      SCIC_SDS_CONTROLLER_PHY_START_TIMEOUT
   );

   this_controller->phy_startup_timer_pending = TRUE;
}

/**
 * @brief
 *
 * @param[in] this_controller
 */
void scic_sds_controller_phy_timer_stop(
   SCIC_SDS_CONTROLLER_T *this_controller
)
{
   scic_cb_timer_stop(
      this_controller,
      this_controller->phy_startup_timer
   );

   this_controller->phy_startup_timer_pending = FALSE;
}

/**
 * @brief This method is called internally to determine whether the
 *        controller start process is complete.  This is only true when:
 *          - all links have been given an opportunity to start
 *          - have no indication of a connected device
 *          - have an indication of a connected device and it has
 *             finished the link training process.
 *
 * @param[in] this_controller This parameter specifies the controller
 *            object for which to start the next phy.
 *
 * @return BOOL
 */
BOOL scic_sds_controller_is_start_complete(
   SCIC_SDS_CONTROLLER_T *this_controller
)
{
   U8 index;

   for (index = 0; index < SCI_MAX_PHYS; index++)
   {
      SCIC_SDS_PHY_T *the_phy = & this_controller->phy_table[index];

      if (
            (
                  this_controller->oem_parameters.sds1.controller.mode_type
               == SCIC_PORT_AUTOMATIC_CONFIGURATION_MODE
            )
         || (
               (
                  this_controller->oem_parameters.sds1.controller.mode_type
               == SCIC_PORT_MANUAL_CONFIGURATION_MODE
               )
            && (scic_sds_phy_get_port(the_phy) != SCI_INVALID_HANDLE)
            )
         )
      {
         /**
          * The controller start operation is complete if and only
          * if:
          * - all links have been given an opportunity to start
          * - have no indication of a connected device
          * - have an indication of a connected device and it has
          *   finished the link training process.
          */
        if (
               (
                  (the_phy->is_in_link_training == FALSE)
               && (the_phy->parent.state_machine.current_state_id
                   == SCI_BASE_PHY_STATE_INITIAL)
               )
            || (
                  (the_phy->is_in_link_training == FALSE)
               && (the_phy->parent.state_machine.current_state_id
                   == SCI_BASE_PHY_STATE_STOPPED)
               )
            || (
                  (the_phy->is_in_link_training == TRUE)
               && (the_phy->parent.state_machine.current_state_id
                   == SCI_BASE_PHY_STATE_STARTING)
               )
            || (
                  this_controller->port_agent.phy_ready_mask
                  != this_controller->port_agent.phy_configured_mask
               )
            )
         {
            return FALSE;
         }
      }
   }

   return TRUE;
}

/**
 * @brief This method is called internally by the controller object to
 *        start the next phy on the controller.  If all the phys have
 *        been starte, then this method will attempt to transition the
 *        controller to the READY state and inform the user
 *        (scic_cb_controller_start_complete()).
 *
 * @param[in] this_controller This parameter specifies the controller
 *            object for which to start the next phy.
 *
 * @return SCI_STATUS
 */
SCI_STATUS scic_sds_controller_start_next_phy(
   SCIC_SDS_CONTROLLER_T *this_controller
)
{
   SCI_STATUS status;

   status = SCI_SUCCESS;

   if (this_controller->phy_startup_timer_pending == FALSE)
   {
      if (this_controller->next_phy_to_start == SCI_MAX_PHYS)
      {
         // The controller has successfully finished the start process.
         // Inform the SCI Core user and transition to the READY state.
         if (scic_sds_controller_is_start_complete(this_controller) == TRUE)
         {
            scic_sds_controller_transition_to_ready(
               this_controller, SCI_SUCCESS
            );
         }
      }
      else
      {
         SCIC_SDS_PHY_T * the_phy;

         the_phy = &this_controller->phy_table[this_controller->next_phy_to_start];

         if (
               this_controller->oem_parameters.sds1.controller.mode_type
            == SCIC_PORT_MANUAL_CONFIGURATION_MODE
            )
         {
            if (scic_sds_phy_get_port(the_phy) == SCI_INVALID_HANDLE)
            {
               this_controller->next_phy_to_start++;

               // Caution recursion ahead be forwarned
               //
               // The PHY was never added to a PORT in MPC mode so start the next phy in sequence
               // This phy will never go link up and will not draw power the OEM parameters either
               // configured the phy incorrectly for the PORT or it was never assigned to a PORT
               return scic_sds_controller_start_next_phy(this_controller);
            }
         }

         status = scic_phy_start(the_phy);

         if (status == SCI_SUCCESS)
         {
            scic_sds_controller_phy_timer_start(this_controller);
         }
         else
         {
            SCIC_LOG_WARNING((
               sci_base_object_get_logger(this_controller),
               SCIC_LOG_OBJECT_CONTROLLER | SCIC_LOG_OBJECT_PHY,
               "Controller stop operation failed to stop phy %d because of status %d.\n",
               this_controller->phy_table[this_controller->next_phy_to_start].phy_index,
               status
            ));
         }

         this_controller->next_phy_to_start++;
      }
   }

   return status;
}

/**
 * @brief
 *
 * @param[in] this_controller
 *
 * @return SCI_STATUS
 */
SCI_STATUS scic_sds_controller_stop_phys(
   SCIC_SDS_CONTROLLER_T *this_controller
)
{
   U32        index;
   SCI_STATUS status;
   SCI_STATUS phy_status;

   status = SCI_SUCCESS;

   for (index = 0; index < SCI_MAX_PHYS; index++)
   {
      phy_status = scic_phy_stop(&this_controller->phy_table[index]);

      if (
              (phy_status != SCI_SUCCESS)
           && (phy_status != SCI_FAILURE_INVALID_STATE)
         )
      {
         status = SCI_FAILURE;

         SCIC_LOG_WARNING((
            sci_base_object_get_logger(this_controller),
            SCIC_LOG_OBJECT_CONTROLLER | SCIC_LOG_OBJECT_PHY,
            "Controller stop operation failed to stop phy %d because of status %d.\n",
            this_controller->phy_table[index].phy_index, phy_status
         ));
      }
   }

   return status;
}

/**
 * @brief
 *
 * @param[in] this_controller
 *
 * @return SCI_STATUS
 */
SCI_STATUS scic_sds_controller_stop_devices(
   SCIC_SDS_CONTROLLER_T *this_controller
)
{
   U32        index;
   SCI_STATUS status;
   SCI_STATUS device_status;

   status = SCI_SUCCESS;

   for (index = 0; index < this_controller->remote_node_entries; index++)
   {
      if (this_controller->device_table[index] != SCI_INVALID_HANDLE)
      {
         /// @todo What timeout value do we want to provide to this request?
         device_status = scic_remote_device_stop(this_controller->device_table[index], 0);

         if (
                 (device_status != SCI_SUCCESS)
              && (device_status != SCI_FAILURE_INVALID_STATE)
            )
         {
            SCIC_LOG_WARNING((
               sci_base_object_get_logger(this_controller),
               SCIC_LOG_OBJECT_CONTROLLER | SCIC_LOG_OBJECT_SSP_REMOTE_TARGET,
               "Controller stop operation failed to stop device 0x%x because of status %d.\n",
               this_controller->device_table[index], device_status
            ));
         }
      }
   }

   return status;
}

//****************************************************************************-
//* SCIC SDS Controller Power Control (Staggered Spinup)
//****************************************************************************-

/**
 * This method starts the power control timer for this controller object.
 *
 * @param this_controller
 */
static
void scic_sds_controller_power_control_timer_start(
   SCIC_SDS_CONTROLLER_T *this_controller
)
{
   scic_cb_timer_start(
      this_controller, this_controller->power_control.timer,
      SCIC_SDS_CONTROLLER_POWER_CONTROL_INTERVAL
   );

   this_controller->power_control.timer_started = TRUE;
}

/**
 * This method stops the power control timer for this controller object.
 *
 * @param this_controller
 */
static
void scic_sds_controller_power_control_timer_stop(
   SCIC_SDS_CONTROLLER_T *this_controller
)
{
   if (this_controller->power_control.timer_started)
   {
      scic_cb_timer_stop(
         this_controller, this_controller->power_control.timer
      );

      this_controller->power_control.timer_started = FALSE;
   }
}

/**
 * This method stops and starts the power control timer for this controller object.
 *
 * @param this_controller
 */
static
void scic_sds_controller_power_control_timer_restart(
   SCIC_SDS_CONTROLLER_T *this_controller
)
{
   scic_sds_controller_power_control_timer_stop(this_controller);
   scic_sds_controller_power_control_timer_start(this_controller);
}


/**
 * @brief
 *
 * @param[in] controller
 */
void scic_sds_controller_power_control_timer_handler(
   void *controller
)
{
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   this_controller->power_control.remote_devices_granted_power = 0;

   if (this_controller->power_control.phys_waiting == 0)
   {
      this_controller->power_control.timer_started = FALSE;
   }
   else
   {
      SCIC_SDS_PHY_T *the_phy = NULL;
      U8 i;

      for (i=0;
              (i < SCI_MAX_PHYS)
           && (this_controller->power_control.phys_waiting != 0);
           i++)
      {
         if (this_controller->power_control.requesters[i] != NULL)
         {
            if ( this_controller->power_control.remote_devices_granted_power <
                 this_controller->oem_parameters.sds1.controller.max_number_concurrent_device_spin_up
               )
            {
               the_phy = this_controller->power_control.requesters[i];
               this_controller->power_control.requesters[i] = NULL;
               this_controller->power_control.phys_waiting--;
               this_controller->power_control.remote_devices_granted_power ++;
               scic_sds_phy_consume_power_handler(the_phy);

               if (the_phy->protocol == SCIC_SDS_PHY_PROTOCOL_SAS)
               {
                  U8 j;
                  SCIC_SDS_PHY_T * current_requester_phy;

                  for (j = 0; j < SCI_MAX_PHYS; j++)
                  {
                     current_requester_phy = this_controller->power_control.requesters[j];

                     //Search the power_control queue to see if there are other phys attached to
                     //the same remote device. If found, take all of them out of await_sas_power state.
                     if (current_requester_phy != NULL &&
                         current_requester_phy != the_phy &&
                         current_requester_phy->phy_type.sas.identify_address_frame_buffer.sas_address.high
                            == the_phy->phy_type.sas.identify_address_frame_buffer.sas_address.high &&
                         current_requester_phy->phy_type.sas.identify_address_frame_buffer.sas_address.low
                            == the_phy->phy_type.sas.identify_address_frame_buffer.sas_address.low)
                     {
                        this_controller->power_control.requesters[j] = NULL;
                        this_controller->power_control.phys_waiting--;
                        scic_sds_phy_consume_power_handler(current_requester_phy);
                     }
                  }
               }
            }
            else
            {
               break;
            }
         }
      }

      // It doesn't matter if the power list is empty, we need to start the
      // timer in case another phy becomes ready.
      scic_sds_controller_power_control_timer_start(this_controller);
   }
}

/**
 * @brief This method inserts the phy in the stagger spinup control queue.
 *
 * @param[in] this_controller
 * @param[in] the_phy
 */
void scic_sds_controller_power_control_queue_insert(
   SCIC_SDS_CONTROLLER_T *this_controller,
   SCIC_SDS_PHY_T        *the_phy
)
{
   ASSERT (the_phy != NULL);

   if( this_controller->power_control.remote_devices_granted_power <
       this_controller->oem_parameters.sds1.controller.max_number_concurrent_device_spin_up
     )
   {
      this_controller->power_control.remote_devices_granted_power ++;
      scic_sds_phy_consume_power_handler(the_phy);

      //stop and start the power_control timer. When the timer fires, the
      //no_of_devices_granted_power will be set to 0
      scic_sds_controller_power_control_timer_restart (this_controller);
   }
   else
   {
      //there are phys, attached to the same sas address as this phy, are already
      //in READY state, this phy don't need wait.
      U8 i;
      SCIC_SDS_PHY_T * current_phy;
      for(i = 0; i < SCI_MAX_PHYS; i++)
      {
         current_phy = &this_controller->phy_table[i];

         if (current_phy->parent.state_machine.current_state_id == SCI_BASE_PHY_STATE_READY &&
             current_phy->protocol == SCIC_SDS_PHY_PROTOCOL_SAS &&
             current_phy->phy_type.sas.identify_address_frame_buffer.sas_address.high
                == the_phy->phy_type.sas.identify_address_frame_buffer.sas_address.high &&
             current_phy->phy_type.sas.identify_address_frame_buffer.sas_address.low
                == the_phy->phy_type.sas.identify_address_frame_buffer.sas_address.low)
         {
            scic_sds_phy_consume_power_handler(the_phy);
            break;
         }
      }

      if (i == SCI_MAX_PHYS)
      {
         //Add the phy in the waiting list
         this_controller->power_control.requesters[the_phy->phy_index] = the_phy;
         this_controller->power_control.phys_waiting++;
      }
   }
}

/**
 * @brief This method removes the phy from the stagger spinup control
 *        queue.
 *
 * @param[in] this_controller
 * @param[in] the_phy
 */
void scic_sds_controller_power_control_queue_remove(
   SCIC_SDS_CONTROLLER_T *this_controller,
   SCIC_SDS_PHY_T        *the_phy
)
{
   ASSERT (the_phy != NULL);

   if (this_controller->power_control.requesters[the_phy->phy_index] != NULL)
   {
      this_controller->power_control.phys_waiting--;
   }

   this_controller->power_control.requesters[the_phy->phy_index] = NULL;
}

//****************************************************************************-
//* SCIC SDS Controller Completion Routines
//****************************************************************************-

/**
 * @brief This method returns a TRUE value if the completion queue has
 *        entries that can be processed
 *
 * @param[in] this_controller
 *
 * @return BOOL
 * @retval TRUE if the completion queue has entries to process
 *         FALSE if the completion queue has no entries to process
 */
static
BOOL scic_sds_controller_completion_queue_has_entries(
   SCIC_SDS_CONTROLLER_T *this_controller
)
{
   U32 get_value = this_controller->completion_queue_get;
   U32 get_index = get_value & SMU_COMPLETION_QUEUE_GET_POINTER_MASK;
   if (
           NORMALIZE_GET_POINTER_CYCLE_BIT(get_value)
        == COMPLETION_QUEUE_CYCLE_BIT(this_controller->completion_queue[get_index])
      )
   {
      return TRUE;
   }

   return FALSE;
}

// ---------------------------------------------------------------------------

/**
 * @brief This method processes a task completion notification.  This is
 *        called from within the controller completion handler.
 *
 * @param[in] this_controller
 * @param[in] completion_entry
 *
 * @return none
 */
static
void scic_sds_controller_task_completion(
   SCIC_SDS_CONTROLLER_T *this_controller,
   U32                   completion_entry
)
{
   U32 index;
   SCIC_SDS_REQUEST_T *io_request;

   index = SCU_GET_COMPLETION_INDEX(completion_entry);
   io_request = this_controller->io_request_table[index];

   // Make sure that we really want to process this IO request
   if (
           (io_request != SCI_INVALID_HANDLE)
        && (io_request->io_tag != SCI_CONTROLLER_INVALID_IO_TAG)
        && (
                scic_sds_io_tag_get_sequence(io_request->io_tag)
             == this_controller->io_request_sequence[index]
           )
      )
   {
      // Yep this is a valid io request pass it along to the io request handler
      scic_sds_io_request_tc_completion(io_request, completion_entry);
   }
}

/**
 * @brief This method processes an SDMA completion event.  This is called
 *        from within the controller completion handler.
 *
 * @param[in] this_controller
 * @param[in] completion_entry
 *
 * @return none
 */
static
void scic_sds_controller_sdma_completion(
   SCIC_SDS_CONTROLLER_T *this_controller,
   U32                   completion_entry
)
{
   U32 index;
   SCIC_SDS_REQUEST_T       *io_request;
   SCIC_SDS_REMOTE_DEVICE_T *device;

   index = SCU_GET_COMPLETION_INDEX(completion_entry);

   switch (scu_get_command_request_type(completion_entry))
   {
   case SCU_CONTEXT_COMMAND_REQUEST_TYPE_POST_TC:
   case SCU_CONTEXT_COMMAND_REQUEST_TYPE_DUMP_TC:
      io_request = this_controller->io_request_table[index];
      SCIC_LOG_ERROR((
         sci_base_object_get_logger(this_controller),
           SCIC_LOG_OBJECT_CONTROLLER
         | SCIC_LOG_OBJECT_SMP_IO_REQUEST
         | SCIC_LOG_OBJECT_SSP_IO_REQUEST
         | SCIC_LOG_OBJECT_STP_IO_REQUEST,
         "SCIC SDS Completion type SDMA %x for io request %x\n",
         completion_entry,
         io_request
      ));
      /// @todo For a post TC operation we need to fail the IO request
      break;

   case SCU_CONTEXT_COMMAND_REQUEST_TYPE_DUMP_RNC:
   case SCU_CONTEXT_COMMAND_REQUEST_TYPE_OTHER_RNC:
   case SCU_CONTEXT_COMMAND_REQUEST_TYPE_POST_RNC:
      device = this_controller->device_table[index];
      SCIC_LOG_ERROR((
         sci_base_object_get_logger(this_controller),
           SCIC_LOG_OBJECT_CONTROLLER
         | SCIC_LOG_OBJECT_SSP_REMOTE_TARGET
         | SCIC_LOG_OBJECT_SMP_REMOTE_TARGET
         | SCIC_LOG_OBJECT_STP_REMOTE_TARGET,
         "SCIC SDS Completion type SDMA %x for remote device %x\n",
         completion_entry,
         device
      ));
      /// @todo For a port RNC operation we need to fail the device
      break;

   default:
      SCIC_LOG_ERROR((
         sci_base_object_get_logger(this_controller),
         SCIC_LOG_OBJECT_CONTROLLER,
         "SCIC SDS Completion unknown SDMA completion type %x\n",
         completion_entry
      ));
      break;
   }

   /// This is an unexpected completion type and is un-recoverable
   /// Transition to the failed state and wait for a controller reset
   sci_base_state_machine_change_state(
      scic_sds_controller_get_base_state_machine(this_controller),
      SCI_BASE_CONTROLLER_STATE_FAILED
   );
}

/**
 * This method processes an unsolicited frame message.  This is called from
 * within the controller completion handler.
 *
 * @param[in] this_controller
 * @param[in] completion_entry
 *
 * @return none
 */
static
void scic_sds_controller_unsolicited_frame(
   SCIC_SDS_CONTROLLER_T *this_controller,
   U32                   completion_entry
)
{
   U32 index;
   U32 frame_index;

   SCU_UNSOLICITED_FRAME_HEADER_T * frame_header;
   SCIC_SDS_PHY_T                 * phy;
   SCIC_SDS_REMOTE_DEVICE_T       * device;

   SCI_STATUS result = SCI_FAILURE;

   frame_index = SCU_GET_FRAME_INDEX(completion_entry);

   frame_header
      = this_controller->uf_control.buffers.array[frame_index].header;
   this_controller->uf_control.buffers.array[frame_index].state
      = UNSOLICITED_FRAME_IN_USE;

   if (SCU_GET_FRAME_ERROR(completion_entry))
   {
      /// @todo If the IAF frame or SIGNATURE FIS frame has an error will
      ///       this cause a problem? We expect the phy initialization will
      ///       fail if there is an error in the frame.
      scic_sds_controller_release_frame(this_controller, frame_index);
      return;
   }

   if (frame_header->is_address_frame)
   {
      index = SCU_GET_PROTOCOL_ENGINE_INDEX(completion_entry);
      phy = &this_controller->phy_table[index];
      if (phy != NULL)
      {
         result = scic_sds_phy_frame_handler(phy, frame_index);
      }
   }
   else
   {

      index = SCU_GET_COMPLETION_INDEX(completion_entry);

      if (index == SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX)
      {
         // This is a signature fis or a frame from a direct attached SATA
         // device that has not yet been created.  In either case forwared
         // the frame to the PE and let it take care of the frame data.
         index = SCU_GET_PROTOCOL_ENGINE_INDEX(completion_entry);
         phy = &this_controller->phy_table[index];
         result = scic_sds_phy_frame_handler(phy, frame_index);
      }
      else
      {
         if (index < this_controller->remote_node_entries)
            device = this_controller->device_table[index];
         else
            device = NULL;

         if (device != NULL)
            result = scic_sds_remote_device_frame_handler(device, frame_index);
         else
            scic_sds_controller_release_frame(this_controller, frame_index);
      }
   }

   if (result != SCI_SUCCESS)
   {
      /// @todo Is there any reason to report some additional error message
      ///       when we get this failure notifiction?
   }
}

/**
 * @brief This method processes an event completion entry.  This is called
 *        from within the controller completion handler.
 *
 * @param[in] this_controller
 * @param[in] completion_entry
 *
 * @return none
 */
static
void scic_sds_controller_event_completion(
   SCIC_SDS_CONTROLLER_T *this_controller,
   U32                   completion_entry
)
{
   U32 index;
   SCIC_SDS_REQUEST_T       *io_request;
   SCIC_SDS_REMOTE_DEVICE_T *device;
   SCIC_SDS_PHY_T           *phy;

   index = SCU_GET_COMPLETION_INDEX(completion_entry);

   switch (scu_get_event_type(completion_entry))
   {
   case SCU_EVENT_TYPE_SMU_COMMAND_ERROR:
      /// @todo The driver did something wrong and we need to fix the condtion.
      SCIC_LOG_ERROR((
         sci_base_object_get_logger(this_controller),
         SCIC_LOG_OBJECT_CONTROLLER,
         "SCIC Controller 0x%x received SMU command error 0x%x\n",
         this_controller, completion_entry
      ));
      break;

   case SCU_EVENT_TYPE_FATAL_MEMORY_ERROR:
       // report fatal memory error
       this_controller->parent.error = SCI_CONTROLLER_FATAL_MEMORY_ERROR;

       sci_base_state_machine_change_state(
          scic_sds_controller_get_base_state_machine(this_controller),
          SCI_BASE_CONTROLLER_STATE_FAILED
       );

       //continue as in following events
   case SCU_EVENT_TYPE_SMU_PCQ_ERROR:
   case SCU_EVENT_TYPE_SMU_ERROR:
      SCIC_LOG_ERROR((
         sci_base_object_get_logger(this_controller),
         SCIC_LOG_OBJECT_CONTROLLER,
         "SCIC Controller 0x%x received fatal controller event 0x%x\n",
         this_controller, completion_entry
      ));
      break;

   case SCU_EVENT_TYPE_TRANSPORT_ERROR:
      io_request = this_controller->io_request_table[index];
      scic_sds_io_request_event_handler(io_request, completion_entry);
      break;

   case SCU_EVENT_TYPE_PTX_SCHEDULE_EVENT:
      switch (scu_get_event_specifier(completion_entry))
      {
      case SCU_EVENT_SPECIFIC_SMP_RESPONSE_NO_PE:
      case SCU_EVENT_SPECIFIC_TASK_TIMEOUT:
         io_request = this_controller->io_request_table[index];
         if (io_request != SCI_INVALID_HANDLE)
         {
            scic_sds_io_request_event_handler(io_request, completion_entry);
         }
         else
         {
            SCIC_LOG_WARNING((
               sci_base_object_get_logger(this_controller),
               SCIC_LOG_OBJECT_CONTROLLER |
               SCIC_LOG_OBJECT_SMP_IO_REQUEST |
               SCIC_LOG_OBJECT_SSP_IO_REQUEST |
               SCIC_LOG_OBJECT_STP_IO_REQUEST,
               "SCIC Controller 0x%x received event 0x%x for io request object that doesnt exist.\n",
               this_controller, completion_entry
            ));
         }
         break;

      case SCU_EVENT_SPECIFIC_IT_NEXUS_TIMEOUT:
         device = this_controller->device_table[index];
         if (device != SCI_INVALID_HANDLE)
         {
            scic_sds_remote_device_event_handler(device, completion_entry);
         }
         else
         {
            SCIC_LOG_WARNING((
               sci_base_object_get_logger(this_controller),
               SCIC_LOG_OBJECT_CONTROLLER |
               SCIC_LOG_OBJECT_SMP_REMOTE_TARGET |
               SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
               SCIC_LOG_OBJECT_STP_REMOTE_TARGET,
               "SCIC Controller 0x%x received event 0x%x for remote device object that doesnt exist.\n",
               this_controller, completion_entry
            ));
         }
         break;
      }
      break;

   case SCU_EVENT_TYPE_BROADCAST_CHANGE:
      // direct the broadcast change event to the phy first and then let
      // the phy redirect the broadcast change to the port object
   case SCU_EVENT_TYPE_ERR_CNT_EVENT:
      // direct error counter event to the phy object since that is where
      // we get the event notification.  This is a type 4 event.
   case SCU_EVENT_TYPE_OSSP_EVENT:
      index = SCU_GET_PROTOCOL_ENGINE_INDEX(completion_entry);
      phy = &this_controller->phy_table[index];
      scic_sds_phy_event_handler(phy, completion_entry);
      break;

   case SCU_EVENT_TYPE_RNC_SUSPEND_TX:
   case SCU_EVENT_TYPE_RNC_SUSPEND_TX_RX:
   case SCU_EVENT_TYPE_RNC_OPS_MISC:
      if (index < this_controller->remote_node_entries)
      {
         device = this_controller->device_table[index];

         if (device != NULL)
         {
            scic_sds_remote_device_event_handler(device, completion_entry);
         }
      }
      else
      {
         SCIC_LOG_ERROR((
            sci_base_object_get_logger(this_controller),
            SCIC_LOG_OBJECT_CONTROLLER |
            SCIC_LOG_OBJECT_SMP_REMOTE_TARGET |
            SCIC_LOG_OBJECT_SSP_REMOTE_TARGET |
            SCIC_LOG_OBJECT_STP_REMOTE_TARGET,
            "SCIC Controller 0x%x received event 0x%x for remote device object 0x%0x that doesnt exist.\n",
            this_controller, completion_entry, index
         ));
      }
      break;

   default:
      SCIC_LOG_WARNING((
         sci_base_object_get_logger(this_controller),
         SCIC_LOG_OBJECT_CONTROLLER,
         "SCIC Controller received unknown event code %x\n",
         completion_entry
      ));
      break;
   }
}

/**
 * @brief This method is a private routine for processing the completion
 *        queue entries.
 *
 * @param[in] this_controller
 *
 * @return none
 */
static
void scic_sds_controller_process_completions(
   SCIC_SDS_CONTROLLER_T *this_controller
)
{
   U32 completion_count = 0;
   U32 completion_entry;
   U32 get_index;
   U32 get_cycle;
   U32 event_index;
   U32 event_cycle;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_controller),
      SCIC_LOG_OBJECT_CONTROLLER,
      "scic_sds_controller_process_completions(0x%x) enter\n",
      this_controller
   ));

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_controller),
      SCIC_LOG_OBJECT_COMPLETION_QUEUE,
      "completion queue beginning get : 0x%08x\n",
      this_controller->completion_queue_get
   ));

   // Get the component parts of the completion queue
   get_index = NORMALIZE_GET_POINTER(this_controller->completion_queue_get);
   get_cycle = SMU_CQGR_CYCLE_BIT & this_controller->completion_queue_get;

   event_index = NORMALIZE_EVENT_POINTER(this_controller->completion_queue_get);
   event_cycle = SMU_CQGR_EVENT_CYCLE_BIT & this_controller->completion_queue_get;

   while (
               NORMALIZE_GET_POINTER_CYCLE_BIT(get_cycle)
            == COMPLETION_QUEUE_CYCLE_BIT(this_controller->completion_queue[get_index])
         )
   {
      completion_count++;

      completion_entry = this_controller->completion_queue[get_index];
      INCREMENT_COMPLETION_QUEUE_GET(this_controller, get_index, get_cycle);

      SCIC_LOG_TRACE((
         sci_base_object_get_logger(this_controller),
         SCIC_LOG_OBJECT_COMPLETION_QUEUE,
         "completion queue entry : 0x%08x\n",
         completion_entry
      ));

      switch (SCU_GET_COMPLETION_TYPE(completion_entry))
      {
      case SCU_COMPLETION_TYPE_TASK:
         scic_sds_controller_task_completion(this_controller, completion_entry);
         break;

      case SCU_COMPLETION_TYPE_SDMA:
         scic_sds_controller_sdma_completion(this_controller, completion_entry);
         break;

      case SCU_COMPLETION_TYPE_UFI:
         scic_sds_controller_unsolicited_frame(this_controller, completion_entry);
         break;

      case SCU_COMPLETION_TYPE_EVENT:
         scic_sds_controller_event_completion(this_controller, completion_entry);
         break;

      case SCU_COMPLETION_TYPE_NOTIFY:
         // Presently we do the same thing with a notify event that we do with the
         // other event codes.
         INCREMENT_EVENT_QUEUE_GET(this_controller, event_index, event_cycle);
         scic_sds_controller_event_completion(this_controller, completion_entry);
         break;

      default:
         SCIC_LOG_WARNING((
            sci_base_object_get_logger(this_controller),
            SCIC_LOG_OBJECT_CONTROLLER,
            "SCIC Controller received unknown completion type %x\n",
            completion_entry
         ));
         break;
      }
   }

   // Update the get register if we completed one or more entries
   if (completion_count > 0)
   {
      this_controller->completion_queue_get =
           SMU_CQGR_GEN_BIT(ENABLE)
         | SMU_CQGR_GEN_BIT(EVENT_ENABLE)
         | event_cycle | SMU_CQGR_GEN_VAL(EVENT_POINTER, event_index)
         | get_cycle   | SMU_CQGR_GEN_VAL(POINTER, get_index)  ;

      SMU_CQGR_WRITE(this_controller, this_controller->completion_queue_get);
   }

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_controller),
      SCIC_LOG_OBJECT_COMPLETION_QUEUE,
      "completion queue ending get : 0x%08x\n",
      this_controller->completion_queue_get
   ));

}

/**
 * @brief This method is a private routine for processing the completion
 *        queue entries.
 *
 * @param[in] this_controller
 *
 * @return none
 */
static
void scic_sds_controller_transitioned_process_completions(
   SCIC_SDS_CONTROLLER_T * this_controller
)
{
   U32 completion_count = 0;
   U32 completion_entry;
   U32 get_index;
   U32 get_cycle;
   U32 event_index;
   U32 event_cycle;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_controller),
      SCIC_LOG_OBJECT_CONTROLLER,
      "scic_sds_controller_transitioned_process_completions(0x%x) enter\n",
      this_controller
   ));

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_controller),
      SCIC_LOG_OBJECT_COMPLETION_QUEUE,
      "completion queue beginning get : 0x%08x\n",
      this_controller->completion_queue_get
   ));

   // Get the component parts of the completion queue
   get_index = NORMALIZE_GET_POINTER(this_controller->completion_queue_get);
   get_cycle = SMU_CQGR_CYCLE_BIT & this_controller->completion_queue_get;

   event_index = NORMALIZE_EVENT_POINTER(this_controller->completion_queue_get);
   event_cycle = SMU_CQGR_EVENT_CYCLE_BIT & this_controller->completion_queue_get;

   while (
               NORMALIZE_GET_POINTER_CYCLE_BIT(get_cycle)
            == COMPLETION_QUEUE_CYCLE_BIT(
                  this_controller->completion_queue[get_index])
         )
   {
      completion_count++;

      completion_entry = this_controller->completion_queue[get_index];
      INCREMENT_COMPLETION_QUEUE_GET(this_controller, get_index, get_cycle);

      SCIC_LOG_TRACE((
         sci_base_object_get_logger(this_controller),
         SCIC_LOG_OBJECT_COMPLETION_QUEUE,
         "completion queue entry : 0x%08x\n",
         completion_entry
      ));

      switch (SCU_GET_COMPLETION_TYPE(completion_entry))
      {
      case SCU_COMPLETION_TYPE_TASK:
         scic_sds_controller_task_completion(this_controller, completion_entry);
      break;

      case SCU_COMPLETION_TYPE_NOTIFY:
         INCREMENT_EVENT_QUEUE_GET(this_controller, event_index, event_cycle);
         // Fall-through

      case SCU_COMPLETION_TYPE_EVENT:
      case SCU_COMPLETION_TYPE_SDMA:
      case SCU_COMPLETION_TYPE_UFI:
      default:
         SCIC_LOG_WARNING((
            sci_base_object_get_logger(this_controller),
            SCIC_LOG_OBJECT_CONTROLLER,
            "SCIC Controller ignoring completion type %x\n",
            completion_entry
         ));
      break;
      }
   }

   // Update the get register if we completed one or more entries
   if (completion_count > 0)
   {
      this_controller->completion_queue_get =
           SMU_CQGR_GEN_BIT(ENABLE)
         | SMU_CQGR_GEN_BIT(EVENT_ENABLE)
         | event_cycle | SMU_CQGR_GEN_VAL(EVENT_POINTER, event_index)
         | get_cycle   | SMU_CQGR_GEN_VAL(POINTER, get_index)  ;

      SMU_CQGR_WRITE(this_controller, this_controller->completion_queue_get);
   }

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_controller),
      SCIC_LOG_OBJECT_COMPLETION_QUEUE,
      "completion queue ending get : 0x%08x\n",
      this_controller->completion_queue_get
   ));
}

//****************************************************************************-
//* SCIC SDS Controller Interrupt and Completion functions
//****************************************************************************-

/**
 * @brief This method provides standard (common) processing of interrupts
 *        for polling and legacy based interrupts.
 *
 * @param[in] controller
 * @param[in] interrupt_status
 *
 * @return This method returns a boolean (BOOL) indication as to
 *         whether an completions are pending to be processed.
 * @retval TRUE if an interrupt is to be processed
 * @retval FALSE if no interrupt was pending
 */
static
BOOL scic_sds_controller_standard_interrupt_handler(
   SCIC_SDS_CONTROLLER_T *this_controller,
   U32                    interrupt_status
)
{
   BOOL  is_completion_needed = FALSE;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_controller),
      SCIC_LOG_OBJECT_CONTROLLER,
      "scic_sds_controller_standard_interrupt_handler(0x%d,0x%d) enter\n",
      this_controller, interrupt_status
   ));

   if (
         (interrupt_status & SMU_ISR_QUEUE_ERROR)
      || (
            (interrupt_status & SMU_ISR_QUEUE_SUSPEND)
         && (!scic_sds_controller_completion_queue_has_entries(this_controller))
         )
      )
   {
      // We have a fatal error on the read of the completion queue bar
      // OR
      // We have a fatal error there is nothing in the completion queue
      // but we have a report from the hardware that the queue is full
      /// @todo how do we request the a controller reset
      is_completion_needed = TRUE;
      this_controller->encountered_fatal_error = TRUE;
   }

   if (scic_sds_controller_completion_queue_has_entries(this_controller))
   {
      is_completion_needed = TRUE;
   }

   return is_completion_needed;
}

/**
 * @brief This is the method provided to handle polling for interrupts
 *        for the controller object.
 *
 * @param[in] controller
 *
 * @return BOOL
 * @retval TRUE if an interrupt is to be processed
 * @retval FALSE if no interrupt was pending
 */
static
BOOL scic_sds_controller_polling_interrupt_handler(
   SCI_CONTROLLER_HANDLE_T controller
)
{
   U32                    interrupt_status;
   SCIC_SDS_CONTROLLER_T *this_controller = (SCIC_SDS_CONTROLLER_T*)controller;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER,
      "scic_sds_controller_polling_interrupt_handler(0x%d) enter\n",
      controller
   ));

   /*
    * In INTERRUPT_POLLING_MODE we exit the interrupt handler if the hardware
    * indicates nothing is pending. Since we are not being called from a real
    * interrupt, we don't want to confuse the hardware by servicing the
    * completion queue before the hardware indicates it is ready. We'll
    * simply wait for another polling interval and check again.
    */
   interrupt_status = SMU_ISR_READ(this_controller);
   if ((interrupt_status &
         (SMU_ISR_COMPLETION |
          SMU_ISR_QUEUE_ERROR |
          SMU_ISR_QUEUE_SUSPEND)) == 0)
   {
      return FALSE;
   }

   return scic_sds_controller_standard_interrupt_handler(
             controller, interrupt_status
          );
}

/**
 * @brief This is the method provided to handle completions when interrupt
 *        polling is in use.
 *
 * @param[in] controller
 *
 * @return none
 */
static
void scic_sds_controller_polling_completion_handler(
   SCI_CONTROLLER_HANDLE_T controller
)
{
   SCIC_SDS_CONTROLLER_T *this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER,
      "scic_sds_controller_polling_completion_handler(0x%d) enter\n",
      controller
   ));

   if (this_controller->encountered_fatal_error == TRUE)
   {
      SCIC_LOG_ERROR((
         sci_base_object_get_logger(this_controller),
         SCIC_LOG_OBJECT_CONTROLLER,
         "SCIC Controller has encountered a fatal error.\n"
      ));

      sci_base_state_machine_change_state(
         scic_sds_controller_get_base_state_machine(this_controller),
         SCI_BASE_CONTROLLER_STATE_FAILED
      );
   }
   else if (scic_sds_controller_completion_queue_has_entries(this_controller))
   {
      if (this_controller->restrict_completions == FALSE)
         scic_sds_controller_process_completions(this_controller);
      else
         scic_sds_controller_transitioned_process_completions(this_controller);
   }

   /*
    * The interrupt handler does not adjust the CQ's
    * get pointer.  So, SCU's INTx pin stays asserted during the
    * interrupt handler even though it tries to clear the interrupt
    * source.  Therefore, the completion handler must ensure that the
    * interrupt source is cleared.  Otherwise, we get a spurious
    * interrupt for which the interrupt handler will not issue a
    * corresponding completion event. Also, we unmask interrupts.
    */
   SMU_ISR_WRITE(
      this_controller,
      (U32)(SMU_ISR_COMPLETION | SMU_ISR_QUEUE_ERROR | SMU_ISR_QUEUE_SUSPEND)
   );
}

#if !defined(DISABLE_INTERRUPTS)
/**
 * @brief This is the method provided to handle legacy interrupts for the
 *        controller object.
 *
 * @param[in] controller
 *
 * @return BOOL
 * @retval TRUE if an interrupt is processed
 *         FALSE if no interrupt was processed
 */
static
BOOL scic_sds_controller_legacy_interrupt_handler(
   SCI_CONTROLLER_HANDLE_T controller
)
{
   U32                    interrupt_status;
   BOOL                   is_completion_needed;
   SCIC_SDS_CONTROLLER_T *this_controller = (SCIC_SDS_CONTROLLER_T*)controller;

   interrupt_status     = SMU_ISR_READ(this_controller);
   is_completion_needed = scic_sds_controller_standard_interrupt_handler(
                             this_controller, interrupt_status
                          );

   return is_completion_needed;
}


/**
 * @brief This is the method provided to handle legacy completions it is
 *        expected that the SCI User will call this completion handler
 *        anytime the interrupt handler reports that it has handled an
 *        interrupt.
 *
 * @param[in] controller
 *
 * @return none
 */
static
void scic_sds_controller_legacy_completion_handler(
   SCI_CONTROLLER_HANDLE_T controller
)
{
   SCIC_SDS_CONTROLLER_T *this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER,
      "scic_sds_controller_legacy_completion_handler(0x%d) enter\n",
      controller
   ));

   scic_sds_controller_polling_completion_handler(controller);

   SMU_IMR_WRITE(this_controller, 0x00000000);

#ifdef IMR_READ_FENCE
   {
      volatile U32 int_mask_value = 0;
      ULONG count = 0;

      /*
       * Temporary code since we have seen with legacy interrupts
       * that interrupts are still masked after clearing the mask
       * above. This may be an Arlington problem or it may be an
       * old driver problem.  Presently this code is turned off
       * since we have not seen this problem recently.
       */
      do
      {
         int_mask_value = SMU_IMR_READ(this_controler);

         if (count++ > 10)
         {
            #ifdef ALLOW_ENTER_DEBUGGER
            __debugbreak();
            #endif
            break;
         }
      } while (int_mask_value != 0);
   }
#endif
}

/**
 * @brief This is the method provided to handle an MSIX interrupt message
 *        when there is just a single MSIX message being provided by the
 *        hardware.  This mode of operation is single vector mode.
 *
 * @param[in] controller
 *
 * @return BOOL
 * @retval TRUE if an interrupt is processed
 *         FALSE if no interrupt was processed
 */
static
BOOL scic_sds_controller_single_vector_interrupt_handler(
   SCI_CONTROLLER_HANDLE_T controller
)
{
   U32 interrupt_status;
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   // Mask the interrupts
   // There is a race in the hardware that could cause us not to be notified
   // of an interrupt completion if we do not take this step.  We will unmask
   // the interrupts in the completion routine.
   SMU_IMR_WRITE(this_controller, 0xFFFFFFFF);

   interrupt_status = SMU_ISR_READ(this_controller);
   interrupt_status &= (SMU_ISR_QUEUE_ERROR | SMU_ISR_QUEUE_SUSPEND);

   if (
           (interrupt_status == 0)
        && scic_sds_controller_completion_queue_has_entries(this_controller)
      )
   {
      // There is at least one completion queue entry to process so we can
      // return a success and ignore for now the case of an error interrupt
      SMU_ISR_WRITE(this_controller, SMU_ISR_COMPLETION);

      return TRUE;
   }


   if (interrupt_status != 0)
   {
      // There is an error interrupt pending so let it through and handle
      // in the callback
      return TRUE;
   }

   // Clear any offending interrupts since we could not find any to handle
   // and unmask them all
   SMU_ISR_WRITE(this_controller, 0x00000000);
   SMU_IMR_WRITE(this_controller, 0x00000000);

   return FALSE;
}

/**
 * @brief This is the method provided to handle completions for a single
 *        MSIX message.
 *
 * @param[in] controller
 */
static
void scic_sds_controller_single_vector_completion_handler(
   SCI_CONTROLLER_HANDLE_T controller
)
{
   U32 interrupt_status;
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER,
      "scic_sds_controller_single_vector_completion_handler(0x%d) enter\n",
      controller
   ));

   interrupt_status = SMU_ISR_READ(this_controller);
   interrupt_status &= (SMU_ISR_QUEUE_ERROR | SMU_ISR_QUEUE_SUSPEND);

   if (interrupt_status & SMU_ISR_QUEUE_ERROR)
   {
      SCIC_LOG_ERROR((
         sci_base_object_get_logger(this_controller),
         SCIC_LOG_OBJECT_CONTROLLER,
         "SCIC Controller has encountered a fatal error.\n"
      ));

      // We have a fatal condition and must reset the controller
      // Leave the interrupt mask in place and get the controller reset
      sci_base_state_machine_change_state(
         scic_sds_controller_get_base_state_machine(this_controller),
         SCI_BASE_CONTROLLER_STATE_FAILED
      );
      return;
   }

   if (
           (interrupt_status & SMU_ISR_QUEUE_SUSPEND)
        && !scic_sds_controller_completion_queue_has_entries(this_controller)
      )
   {
      SCIC_LOG_ERROR((
         sci_base_object_get_logger(this_controller),
         SCIC_LOG_OBJECT_CONTROLLER,
         "SCIC Controller has encountered a fatal error.\n"
      ));

      // We have a fatal condtion and must reset the controller
      // Leave the interrupt mask in place and get the controller reset
      sci_base_state_machine_change_state(
         scic_sds_controller_get_base_state_machine(this_controller),
         SCI_BASE_CONTROLLER_STATE_FAILED
      );
      return;
   }

   if (scic_sds_controller_completion_queue_has_entries(this_controller))
   {
      scic_sds_controller_process_completions(this_controller);

      // We dont care which interrupt got us to processing the completion queu
      // so clear them both.
      SMU_ISR_WRITE(
         this_controller,
         (SMU_ISR_COMPLETION | SMU_ISR_QUEUE_SUSPEND)
      );
   }

   SMU_IMR_WRITE(this_controller, 0x00000000);
}

/**
 * @brief This is the method provided to handle a MSIX message for a normal
 *        completion.
 *
 * @param[in] controller
 *
 * @return BOOL
 * @retval TRUE if an interrupt is processed
 *         FALSE if no interrupt was processed
 */
static
BOOL scic_sds_controller_normal_vector_interrupt_handler(
   SCI_CONTROLLER_HANDLE_T controller
)
{
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   if (scic_sds_controller_completion_queue_has_entries(this_controller))
   {
      return TRUE;
   }
   else
   {
      // we have a spurious interrupt it could be that we have already
      // emptied the completion queue from a previous interrupt
      SMU_ISR_WRITE(this_controller, SMU_ISR_COMPLETION);

      // There is a race in the hardware that could cause us not to be notified
      // of an interrupt completion if we do not take this step.  We will mask
      // then unmask the interrupts so if there is another interrupt pending
      // the clearing of the interrupt source we get the next interrupt message.
      SMU_IMR_WRITE(this_controller, 0xFF000000);
      SMU_IMR_WRITE(this_controller, 0x00000000);
   }

   return FALSE;
}

/**
 * @brief This is the method provided to handle the completions for a
 *        normal MSIX message.
 *
 * @param[in] controller
 */
static
void scic_sds_controller_normal_vector_completion_handler(
   SCI_CONTROLLER_HANDLE_T controller
)
{
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER,
      "scic_sds_controller_normal_vector_completion_handler(0x%d) enter\n",
      controller
   ));

   // Empty out the completion queue
   if (scic_sds_controller_completion_queue_has_entries(this_controller))
   {
      scic_sds_controller_process_completions(this_controller);
   }

   // Clear the interrupt and enable all interrupts again
   SMU_ISR_WRITE(this_controller, SMU_ISR_COMPLETION);
   // Could we write the value of SMU_ISR_COMPLETION?
   SMU_IMR_WRITE(this_controller, 0xFF000000);
   SMU_IMR_WRITE(this_controller, 0x00000000);
}

/**
 * @brief This is the method provided to handle the error MSIX message
 *        interrupt.  This is the normal operating mode for the hardware if
 *        MSIX is enabled.
 *
 * @param[in] controller
 *
 * @return BOOL
 * @retval TRUE if an interrupt is processed
 *         FALSE if no interrupt was processed
 */
static
BOOL scic_sds_controller_error_vector_interrupt_handler(
   SCI_CONTROLLER_HANDLE_T controller
)
{
   U32 interrupt_status;
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;


   interrupt_status = SMU_ISR_READ(this_controller);
   interrupt_status &= (SMU_ISR_QUEUE_ERROR | SMU_ISR_QUEUE_SUSPEND);

   if (interrupt_status != 0)
   {
      // There is an error interrupt pending so let it through and handle
      // in the callback
      return TRUE;
   }

   // There is a race in the hardware that could cause us not to be notified
   // of an interrupt completion if we do not take this step.  We will mask
   // then unmask the error interrupts so if there was another interrupt
   // pending we will be notified.
   // Could we write the value of (SMU_ISR_QUEUE_ERROR | SMU_ISR_QUEUE_SUSPEND)?
   SMU_IMR_WRITE(this_controller, 0x000000FF);
   SMU_IMR_WRITE(this_controller, 0x00000000);

   return FALSE;
}

/**
 * @brief This is the method provided to handle the error completions when
 *        the hardware is using two MSIX messages.
 *
 * @param[in] controller
 */
static
void scic_sds_controller_error_vector_completion_handler(
   SCI_CONTROLLER_HANDLE_T controller
)
{
   U32 interrupt_status;
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER,
      "scic_sds_controller_error_vector_completion_handler(0x%d) enter\n",
      controller
   ));

   interrupt_status = SMU_ISR_READ(this_controller);

   if (
            (interrupt_status & SMU_ISR_QUEUE_SUSPEND)
         && scic_sds_controller_completion_queue_has_entries(this_controller)
      )
   {
      scic_sds_controller_process_completions(this_controller);

      SMU_ISR_WRITE(this_controller, SMU_ISR_QUEUE_SUSPEND);
   }
   else
   {
      SCIC_LOG_ERROR((
         sci_base_object_get_logger(this_controller),
         SCIC_LOG_OBJECT_CONTROLLER,
         "SCIC Controller reports CRC error on completion ISR %x\n",
         interrupt_status
      ));

      sci_base_state_machine_change_state(
         scic_sds_controller_get_base_state_machine(this_controller),
         SCI_BASE_CONTROLLER_STATE_FAILED
      );

      return;
   }

   // If we dont process any completions I am not sure that we want to do this.
   // We are in the middle of a hardware fault and should probably be reset.
   SMU_IMR_WRITE(this_controller, 0x00000000);
}

#endif // !defined(DISABLE_INTERRUPTS)

//****************************************************************************-
//* SCIC SDS Controller External Methods
//****************************************************************************-

/**
 * @brief This method returns the sizeof the SCIC SDS Controller Object
 *
 * @return U32
 */
U32 scic_sds_controller_get_object_size(void)
{
   return sizeof(SCIC_SDS_CONTROLLER_T);
}

/**
 * This method returns the minimum number of timers that are required by the
 * controller object.  This will include required timers for phys and ports.
 *
 * @return U32
 * @retval The minimum number of timers that are required to make this
 *         controller operational.
 */
U32 scic_sds_controller_get_min_timer_count(void)
{
   return   SCIC_SDS_CONTROLLER_MIN_TIMER_COUNT
          + scic_sds_port_get_min_timer_count()
          + scic_sds_phy_get_min_timer_count();
}

/**
 * This method returns the maximum number of timers that are required by the
 * controller object.  This will include required timers for phys and ports.
 *
 * @return U32
 * @retval The maximum number of timers that will be used by the controller
 *         object
 */
U32 scic_sds_controller_get_max_timer_count(void)
{
   return   SCIC_SDS_CONTROLLER_MAX_TIMER_COUNT
          + scic_sds_port_get_max_timer_count()
          + scic_sds_phy_get_max_timer_count();
}

/**
 * @brief
 *
 * @param[in] this_controller
 * @param[in] the_port
 * @param[in] the_phy
 *
 * @return none
 */
void scic_sds_controller_link_up(
   SCIC_SDS_CONTROLLER_T *this_controller,
   SCIC_SDS_PORT_T       *the_port,
   SCIC_SDS_PHY_T        *the_phy
)
{
   if (this_controller->state_handlers->link_up_handler != NULL)
   {
      this_controller->state_handlers->link_up_handler(
         this_controller, the_port, the_phy);
   }
   else
   {
      SCIC_LOG_INFO((
         sci_base_object_get_logger(this_controller),
         SCIC_LOG_OBJECT_CONTROLLER,
         "SCIC Controller linkup event from phy %d in unexpected state %d\n",
         the_phy->phy_index,
         sci_base_state_machine_get_state(
            scic_sds_controller_get_base_state_machine(this_controller))
      ));
   }
}

/**
 * @brief
 *
 * @param[in] this_controller
 * @param[in] the_port
 * @param[in] the_phy
 */
void scic_sds_controller_link_down(
   SCIC_SDS_CONTROLLER_T *this_controller,
   SCIC_SDS_PORT_T       *the_port,
   SCIC_SDS_PHY_T        *the_phy
)
{
   if (this_controller->state_handlers->link_down_handler != NULL)
   {
      this_controller->state_handlers->link_down_handler(
         this_controller, the_port, the_phy);
   }
   else
   {
      SCIC_LOG_INFO((
         sci_base_object_get_logger(this_controller),
         SCIC_LOG_OBJECT_CONTROLLER,
         "SCIC Controller linkdown event from phy %d in unexpected state %d\n",
         the_phy->phy_index,
         sci_base_state_machine_get_state(
            scic_sds_controller_get_base_state_machine(this_controller))
      ));
   }
}

/**
 * @brief This method is called by the remote device to inform the controller
 *        that this remote device has started.
 *
 * @param[in] this_controller
 * @param[in] the_device
 */
void scic_sds_controller_remote_device_started(
   SCIC_SDS_CONTROLLER_T    * this_controller,
   SCIC_SDS_REMOTE_DEVICE_T * the_device
)
{
   if (this_controller->state_handlers->remote_device_started_handler != NULL)
   {
      this_controller->state_handlers->remote_device_started_handler(
         this_controller, the_device
      );
   }
   else
   {
      SCIC_LOG_INFO((
         sci_base_object_get_logger(this_controller),
         SCIC_LOG_OBJECT_CONTROLLER,
         "SCIC Controller 0x%x remote device started event from device 0x%x in unexpected state %d\n",
         this_controller,
         the_device,
         sci_base_state_machine_get_state(
            scic_sds_controller_get_base_state_machine(this_controller))
      ));
   }
}

/**
 * @brief This is a helper method to determine if any remote devices on this
 *        controller are still in the stopping state.
 *
 * @param[in] this_controller
 */
BOOL scic_sds_controller_has_remote_devices_stopping(
   SCIC_SDS_CONTROLLER_T * this_controller
)
{
   U32 index;

   for (index = 0; index < this_controller->remote_node_entries; index++)
   {
      if (
            (this_controller->device_table[index] != NULL)
         && (
               this_controller->device_table[index]->parent.state_machine.current_state_id
            == SCI_BASE_REMOTE_DEVICE_STATE_STOPPING
            )
         )
      {
         return TRUE;
      }
   }

   return FALSE;
}

/**
 * @brief This method is called by the remote device to inform the controller
 *        object that the remote device has stopped.
 *
 * @param[in] this_controller
 * @param[in] the_device
 */
void scic_sds_controller_remote_device_stopped(
   SCIC_SDS_CONTROLLER_T    * this_controller,
   SCIC_SDS_REMOTE_DEVICE_T * the_device
)
{
   if (this_controller->state_handlers->remote_device_stopped_handler != NULL)
   {
      this_controller->state_handlers->remote_device_stopped_handler(
         this_controller, the_device
      );
   }
   else
   {
      SCIC_LOG_INFO((
         sci_base_object_get_logger(this_controller),
         SCIC_LOG_OBJECT_CONTROLLER,
         "SCIC Controller 0x%x remote device stopped event from device 0x%x in unexpected state %d\n",
         this_controller,
         the_device,
         sci_base_state_machine_get_state(
            scic_sds_controller_get_base_state_machine(this_controller))
      ));
   }
}

/**
 * @brief This method will write to the SCU PCP register the request value.
 *        The method is used to suspend/resume ports, devices, and phys.
 *
 * @param[in] this_controller
 * @param[in] request
 */
void scic_sds_controller_post_request(
   SCIC_SDS_CONTROLLER_T *this_controller,
   U32                    request
)
{
   SCIC_LOG_INFO((
      sci_base_object_get_logger(this_controller),
      SCIC_LOG_OBJECT_CONTROLLER | SCIC_LOG_OBJECT_COMPLETION_QUEUE,
      "SCIC Controller 0x%08x post request 0x%08x\n",
      this_controller, request
   ));

   SMU_PCP_WRITE(this_controller, request);
}

/**
 * @brief This method will copy the soft copy of the task context into
 *        the physical memory accessible by the controller.
 *
 * @note After this call is made the SCIC_SDS_IO_REQUEST object will
 *       always point to the physical memory version of the task context.
 *       Thus, all subsequent updates to the task context are performed in
 *       the TC table (i.e. DMAable memory).
 *
 * @param[in]  this_controller This parameter specifies the controller for
 *             which to copy the task context.
 * @param[in]  this_request This parameter specifies the request for which
 *             the task context is being copied.
 *
 * @return none
 */
void scic_sds_controller_copy_task_context(
   SCIC_SDS_CONTROLLER_T *this_controller,
   SCIC_SDS_REQUEST_T    *this_request
)
{
   SCU_TASK_CONTEXT_T *task_context_buffer;

   task_context_buffer = scic_sds_controller_get_task_context_buffer(
                            this_controller, this_request->io_tag
                         );

   memcpy(
      task_context_buffer,
      this_request->task_context_buffer,
      SCI_FIELD_OFFSET(SCU_TASK_CONTEXT_T, sgl_snapshot_ac)
   );

   // Now that the soft copy of the TC has been copied into the TC
   // table accessible by the silicon.  Thus, any further changes to
   // the TC (e.g. TC termination) occur in the appropriate location.
   this_request->task_context_buffer = task_context_buffer;
}

/**
 * @brief This method returns the task context buffer for the given io tag.
 *
 * @param[in] this_controller
 * @param[in] io_tag
 *
 * @return struct SCU_TASK_CONTEXT*
 */
SCU_TASK_CONTEXT_T * scic_sds_controller_get_task_context_buffer(
   SCIC_SDS_CONTROLLER_T * this_controller,
   U16                     io_tag
)
{
   U16 task_index = scic_sds_io_tag_get_index(io_tag);

   if (task_index < this_controller->task_context_entries)
   {
      return &this_controller->task_context_table[task_index];
   }

   return NULL;
}

/**
 * @brief This method returnst the sequence value from the io tag value
 *
 * @param[in] this_controller
 * @param[in] io_tag
 *
 * @return U16
 */
U16 scic_sds_controller_get_io_sequence_from_tag(
   SCIC_SDS_CONTROLLER_T *this_controller,
   U16                    io_tag
)
{
   return scic_sds_io_tag_get_sequence(io_tag);
}

/**
 * @brief This method returns the IO request associated with the tag value
 *
 * @param[in] this_controller
 * @param[in] io_tag
 *
 * @return SCIC_SDS_IO_REQUEST_T*
 * @retval NULL if there is no valid IO request at the tag value
 */
SCIC_SDS_REQUEST_T *scic_sds_controller_get_io_request_from_tag(
   SCIC_SDS_CONTROLLER_T *this_controller,
   U16                    io_tag
)
{
   U16 task_index;
   U16 task_sequence;

   task_index = scic_sds_io_tag_get_index(io_tag);

   if (task_index  < this_controller->task_context_entries)
   {
      if (this_controller->io_request_table[task_index] != SCI_INVALID_HANDLE)
      {
         task_sequence = scic_sds_io_tag_get_sequence(io_tag);

         if (task_sequence == this_controller->io_request_sequence[task_index])
         {
            return this_controller->io_request_table[task_index];
         }
      }
   }

   return SCI_INVALID_HANDLE;
}

/**
 * @brief This method allocates remote node index and the reserves the
 *        remote node context space for use. This method can fail if there
 *        are no more remote node index available.
 *
 * @param[in] this_controller This is the controller object which contains
 *            the set of free remote node ids
 * @param[in] the_devce This is the device object which is requesting the a
 *            remote node id
 * @param[out] node_id This is the remote node id that is assinged to the
 *             device if one is available
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_OUT_OF_RESOURCES if there are no available remote
 *         node index available.
 */
SCI_STATUS scic_sds_controller_allocate_remote_node_context(
   SCIC_SDS_CONTROLLER_T    * this_controller,
   SCIC_SDS_REMOTE_DEVICE_T * the_device,
   U16                      * node_id
)
{
   U16 node_index;
   U32 remote_node_count = scic_sds_remote_device_node_count(the_device);

   node_index = scic_sds_remote_node_table_allocate_remote_node(
                  &this_controller->available_remote_nodes, remote_node_count
              );

   if (node_index != SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX)
   {
      this_controller->device_table[node_index] = the_device;

      *node_id = node_index;

      return SCI_SUCCESS;
   }

   return SCI_FAILURE_INSUFFICIENT_RESOURCES;
}

/**
 * @brief This method frees the remote node index back to the available
 *        pool.  Once this is done the remote node context buffer is no
 *        longer valid and can not be used.
 *
 * @param[in] this_controller
 * @param[in] the_device
 * @param[in] node_id
 *
 * @return none
 */
void scic_sds_controller_free_remote_node_context(
   SCIC_SDS_CONTROLLER_T    * this_controller,
   SCIC_SDS_REMOTE_DEVICE_T * the_device,
   U16                        node_id
)
{
   U32 remote_node_count = scic_sds_remote_device_node_count(the_device);

   if (this_controller->device_table[node_id] == the_device)
   {
      this_controller->device_table[node_id] = SCI_INVALID_HANDLE;

      scic_sds_remote_node_table_release_remote_node_index(
         &this_controller->available_remote_nodes, remote_node_count, node_id
      );
   }
}

/**
 * @brief This method returns the SCU_REMOTE_NODE_CONTEXT for the specified
 *        remote node id.
 *
 * @param[in] this_controller
 * @param[in] node_id
 *
 * @return SCU_REMOTE_NODE_CONTEXT_T*
 */
SCU_REMOTE_NODE_CONTEXT_T *scic_sds_controller_get_remote_node_context_buffer(
   SCIC_SDS_CONTROLLER_T *this_controller,
   U16                    node_id
)
{
   if (
           (node_id < this_controller->remote_node_entries)
        && (this_controller->device_table[node_id] != SCI_INVALID_HANDLE)
      )
   {
      return &this_controller->remote_node_context_table[node_id];
   }

   return NULL;
}

/**
 * This method will combind the frame header and frame buffer to create
 * a SATA D2H register FIS
 *
 * @param[out] resposne_buffer This is the buffer into which the D2H register
 *             FIS will be constructed.
 * @param[in]  frame_header This is the frame header returned by the hardware.
 * @param[in]  frame_buffer This is the frame buffer returned by the hardware.
 *
 * @erturn none
 */
void scic_sds_controller_copy_sata_response(
   void * response_buffer,
   void * frame_header,
   void * frame_buffer
)
{
   memcpy(
      response_buffer,
      frame_header,
      sizeof(U32)
   );

   memcpy(
      (char *)((char *)response_buffer + sizeof(U32)),
      frame_buffer,
      sizeof(SATA_FIS_REG_D2H_T) - sizeof(U32)
   );
}

/**
 * @brief This method releases the frame once this is done the frame is
 *        available for re-use by the hardware.  The data contained in the
 *        frame header and frame buffer is no longer valid.
 *        The UF queue get pointer is only updated if UF control indicates
 *        this is appropriate.
 *
 * @param[in] this_controller
 * @param[in] frame_index
 *
 * @return none
 */
void scic_sds_controller_release_frame(
   SCIC_SDS_CONTROLLER_T *this_controller,
   U32                    frame_index
)
{
   if (scic_sds_unsolicited_frame_control_release_frame(
          &this_controller->uf_control, frame_index) == TRUE)
      SCU_UFQGP_WRITE(this_controller, this_controller->uf_control.get);
}

#ifdef SCI_LOGGING
void scic_sds_controller_initialize_state_logging(
   SCIC_SDS_CONTROLLER_T *this_controller
)
{
   sci_base_state_machine_logger_initialize(
      &this_controller->parent.state_machine_logger,
      &this_controller->parent.state_machine,
      &this_controller->parent.parent,
      scic_cb_logger_log_states,
      "SCIC_SDS_CONTROLLER_T", "base state machine",
      SCIC_LOG_OBJECT_CONTROLLER
   );
}

void scic_sds_controller_deinitialize_state_logging(
   SCIC_SDS_CONTROLLER_T *this_controller
)
{
   sci_base_state_machine_logger_deinitialize(
      &this_controller->parent.state_machine_logger,
      &this_controller->parent.state_machine
   );
}
#endif

/**
 * @brief This method sets user parameters and OEM parameters to
 *        default values.  Users can override these values utilizing
 *        the scic_user_parameters_set() and scic_oem_parameters_set()
 *        methods.
 *
 * @param[in] controller This parameter specifies the controller for
 *            which to set the configuration parameters to their
 *            default values.
 *
 * @return none
 */
static
void scic_sds_controller_set_default_config_parameters(
   SCIC_SDS_CONTROLLER_T *this_controller
)
{
   U16 index;

   // Default to APC mode.
   this_controller->oem_parameters.sds1.controller.mode_type = SCIC_PORT_AUTOMATIC_CONFIGURATION_MODE;

   // Default to 1
   this_controller->oem_parameters.sds1.controller.max_number_concurrent_device_spin_up = 1;

   // Default to no SSC operation.
   this_controller->oem_parameters.sds1.controller.ssc_sata_tx_spread_level = 0;
   this_controller->oem_parameters.sds1.controller.ssc_sas_tx_spread_level  = 0;
   this_controller->oem_parameters.sds1.controller.ssc_sas_tx_type          = 0;

   // Default to all phys to using short cables
   this_controller->oem_parameters.sds1.controller.cable_selection_mask = 0;

   // Initialize all of the port parameter information to narrow ports.
   for (index = 0; index < SCI_MAX_PORTS; index++)
   {
      this_controller->oem_parameters.sds1.ports[index].phy_mask = 0;
   }

   // Initialize all of the phy parameter information.
   for (index = 0; index < SCI_MAX_PHYS; index++)
   {
      // Default to 6G (i.e. Gen 3) for now.  User can override if
      // they choose.
      this_controller->user_parameters.sds1.phys[index].max_speed_generation = 2;

      //the frequencies cannot be 0
      this_controller->user_parameters.sds1.phys[index].align_insertion_frequency = 0x7f;
      this_controller->user_parameters.sds1.phys[index].in_connection_align_insertion_frequency = 0xff;
      this_controller->user_parameters.sds1.phys[index].notify_enable_spin_up_insertion_frequency = 0x33;

      // Previous Vitesse based expanders had a arbitration issue that
      // is worked around by having the upper 32-bits of SAS address
      // with a value greater then the Vitesse company identifier.
      // Hence, usage of 0x5FCFFFFF.
      this_controller->oem_parameters.sds1.phys[index].sas_address.sci_format.high
         = 0x5FCFFFFF;

      // Add in controller index to ensure each controller will have unique SAS addresses by default.
      this_controller->oem_parameters.sds1.phys[index].sas_address.sci_format.low
         = 0x00000001 + this_controller->controller_index;

      if (  (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_A0)
         || (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_A2)
         || (this_controller->pci_revision == SCIC_SDS_PCI_REVISION_B0) )
      {
         this_controller->oem_parameters.sds1.phys[index].afe_tx_amp_control0 = 0x000E7C03;
         this_controller->oem_parameters.sds1.phys[index].afe_tx_amp_control1 = 0x000E7C03;
         this_controller->oem_parameters.sds1.phys[index].afe_tx_amp_control2 = 0x000E7C03;
         this_controller->oem_parameters.sds1.phys[index].afe_tx_amp_control3 = 0x000E7C03;
      }
      else // This must be SCIC_SDS_PCI_REVISION_C0
      {
         this_controller->oem_parameters.sds1.phys[index].afe_tx_amp_control0 = 0x000BDD08;
         this_controller->oem_parameters.sds1.phys[index].afe_tx_amp_control1 = 0x000B7069;
         this_controller->oem_parameters.sds1.phys[index].afe_tx_amp_control2 = 0x000B7C09;
         this_controller->oem_parameters.sds1.phys[index].afe_tx_amp_control3 = 0x000AFC6E;
      }
   }

   this_controller->user_parameters.sds1.stp_inactivity_timeout = 5;
   this_controller->user_parameters.sds1.ssp_inactivity_timeout = 5;
   this_controller->user_parameters.sds1.stp_max_occupancy_timeout = 5;
   this_controller->user_parameters.sds1.ssp_max_occupancy_timeout = 20;
   this_controller->user_parameters.sds1.no_outbound_task_timeout = 20;

}


/**
 * @brief This method release resources in SCI controller.
 *
 * @param[in] this_controller This parameter specifies the core
 *            controller and associated objects whose resources are to be
 *            released.
 *
 * @return This method returns a value indicating if the operation succeeded.
 * @retval SCI_SUCCESS This value indicates that all the timers are destroyed.
 * @retval SCI_FAILURE This value indicates certain failure during the process
 *            of cleaning timer resource.
 */
static
SCI_STATUS scic_sds_controller_release_resource(
   SCIC_SDS_CONTROLLER_T * this_controller
)
{
   SCIC_SDS_PORT_T * port;
   SCIC_SDS_PHY_T * phy;
   U8 index;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_controller),
      SCIC_LOG_OBJECT_CONTROLLER | SCIC_LOG_OBJECT_INITIALIZATION,
      "scic_sds_controller_release_resource(0x%x) enter\n",
      this_controller
   ));

   if(this_controller->phy_startup_timer != NULL)
   {
      scic_cb_timer_destroy(this_controller, this_controller->phy_startup_timer);
      this_controller->phy_startup_timer = NULL;
   }

   if(this_controller->power_control.timer != NULL)
   {
      scic_cb_timer_destroy(this_controller, this_controller->power_control.timer);
      this_controller->power_control.timer = NULL;
   }

   if(this_controller->timeout_timer != NULL)
   {
      scic_cb_timer_destroy(this_controller, this_controller->timeout_timer);
      this_controller->timeout_timer = NULL;
   }

   scic_sds_port_configuration_agent_release_resource(
      this_controller,
      &this_controller->port_agent);

   for(index = 0; index < SCI_MAX_PORTS+1; index++)
   {
      port = &this_controller->port_table[index];
      scic_sds_port_release_resource(this_controller, port);
   }

   for(index = 0; index < SCI_MAX_PHYS; index++)
   {
      phy = &this_controller->phy_table[index];
      scic_sds_phy_release_resource(this_controller, phy);
   }

   return SCI_SUCCESS;
}


/**
 * @brief This method process the ports configured message from port configuration
 *           agent.
 *
 * @param[in] this_controller This parameter specifies the core
 *            controller that its ports are configured.
 *
 * @return None.
 */
void scic_sds_controller_port_agent_configured_ports(
   SCIC_SDS_CONTROLLER_T * this_controller
)
{
   //simply transit to ready. The function below checks the controller state
   scic_sds_controller_transition_to_ready(
      this_controller, SCI_SUCCESS
   );
}


//****************************************************************************-
//* SCIC Controller Public Methods
//****************************************************************************-

SCI_STATUS scic_controller_construct(
   SCI_LIBRARY_HANDLE_T    library,
   SCI_CONTROLLER_HANDLE_T controller,
   void *                  user_object
)
{
   SCIC_SDS_LIBRARY_T    *my_library;
   SCIC_SDS_CONTROLLER_T *this_controller;

   my_library = (SCIC_SDS_LIBRARY_T *)library;
   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(library),
      SCIC_LOG_OBJECT_CONTROLLER | SCIC_LOG_OBJECT_INITIALIZATION,
      "scic_controller_construct(0x%x, 0x%x) enter\n",
      library, controller
   ));

   // Just clear out the memory of the structure to be safe.
   memset(this_controller, 0, sizeof(SCIC_SDS_CONTROLLER_T));

   // Make sure that the static data is assigned before moving onto the
   // base constroller construct as this will cause the controller to
   // enter its initial state and the controller_index and pci_revision
   // will be required to complete those operations correctly
   this_controller->controller_index =
      scic_sds_library_get_controller_index(my_library, this_controller);

   this_controller->pci_revision = my_library->pci_revision;

   sci_base_controller_construct(
      &this_controller->parent,
      sci_base_object_get_logger(my_library),
      scic_sds_controller_state_table,
      this_controller->memory_descriptors,
      ARRAY_SIZE(this_controller->memory_descriptors),
      NULL
   );

   sci_object_set_association(controller, user_object);

   scic_sds_controller_initialize_state_logging(this_controller);

   scic_sds_pci_bar_initialization(this_controller);

   return SCI_SUCCESS;
}

// ---------------------------------------------------------------------------

SCI_STATUS scic_controller_initialize(
   SCI_CONTROLLER_HANDLE_T   controller
)
{
   SCI_STATUS status = SCI_FAILURE_INVALID_STATE;
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER,
      "scic_controller_initialize(0x%x, 0x%d) enter\n",
      controller
   ));

   if (this_controller->state_handlers->parent.initialize_handler != NULL)
   {
      status = this_controller->state_handlers->parent.initialize_handler(
                  (SCI_BASE_CONTROLLER_T *)controller
               );
   }
   else
   {
      SCIC_LOG_WARNING((
         sci_base_object_get_logger(this_controller),
         SCIC_LOG_OBJECT_CONTROLLER,
         "SCIC Controller initialize operation requested in invalid state %d\n",
         sci_base_state_machine_get_state(
            scic_sds_controller_get_base_state_machine(this_controller))
      ));
   }

   return status;
}

// ---------------------------------------------------------------------------

U32 scic_controller_get_suggested_start_timeout(
   SCI_CONTROLLER_HANDLE_T  controller
)
{
   // Validate the user supplied parameters.
   if (controller == SCI_INVALID_HANDLE)
      return 0;

   // The suggested minimum timeout value for a controller start operation:
   //
   //     Signature FIS Timeout
   //   + Phy Start Timeout
   //   + Number of Phy Spin Up Intervals
   //   ---------------------------------
   //   Number of milliseconds for the controller start operation.
   //
   // NOTE: The number of phy spin up intervals will be equivalent
   //       to the number of phys divided by the number phys allowed
   //       per interval - 1 (once OEM parameters are supported).
   //       Currently we assume only 1 phy per interval.

   return (SCIC_SDS_SIGNATURE_FIS_TIMEOUT
           + SCIC_SDS_CONTROLLER_PHY_START_TIMEOUT
           + ((SCI_MAX_PHYS-1) * SCIC_SDS_CONTROLLER_POWER_CONTROL_INTERVAL));
}

// ---------------------------------------------------------------------------

SCI_STATUS scic_controller_start(
   SCI_CONTROLLER_HANDLE_T controller,
   U32 timeout
)
{
   SCI_STATUS status = SCI_FAILURE_INVALID_STATE;
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER,
      "scic_controller_start(0x%x, 0x%d) enter\n",
      controller, timeout
   ));

   if (this_controller->state_handlers->parent.start_handler != NULL)
   {
      status = this_controller->state_handlers->parent.start_handler(
                  (SCI_BASE_CONTROLLER_T *)controller, timeout
               );
   }
   else
   {
      SCIC_LOG_WARNING((
         sci_base_object_get_logger(this_controller),
         SCIC_LOG_OBJECT_CONTROLLER,
         "SCIC Controller start operation requested in invalid state %d\n",
         sci_base_state_machine_get_state(
            scic_sds_controller_get_base_state_machine(this_controller))
      ));
   }

   return status;
}

// ---------------------------------------------------------------------------

SCI_STATUS scic_controller_stop(
   SCI_CONTROLLER_HANDLE_T controller,
   U32 timeout
)
{
   SCI_STATUS status = SCI_FAILURE_INVALID_STATE;
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER,
      "scic_controller_stop(0x%x, 0x%d) enter\n",
      controller, timeout
   ));

   if (this_controller->state_handlers->parent.stop_handler != NULL)
   {
      status = this_controller->state_handlers->parent.stop_handler(
                  (SCI_BASE_CONTROLLER_T *)controller, timeout
               );
   }
   else
   {
      SCIC_LOG_WARNING((
         sci_base_object_get_logger(this_controller),
         SCIC_LOG_OBJECT_CONTROLLER,
         "SCIC Controller stop operation requested in invalid state %d\n",
         sci_base_state_machine_get_state(
            scic_sds_controller_get_base_state_machine(this_controller))
      ));
   }

   return status;
}

// ---------------------------------------------------------------------------

SCI_STATUS scic_controller_reset(
   SCI_CONTROLLER_HANDLE_T controller
)
{
   SCI_STATUS status = SCI_FAILURE_INVALID_STATE;
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER,
      "scic_controller_reset(0x%x) enter\n",
      controller
   ));

   if (this_controller->state_handlers->parent.reset_handler != NULL)
   {
      status = this_controller->state_handlers->parent.reset_handler(
                  (SCI_BASE_CONTROLLER_T *)controller
               );
   }
   else
   {
      SCIC_LOG_WARNING((
         sci_base_object_get_logger(this_controller),
         SCIC_LOG_OBJECT_CONTROLLER,
         "SCIC Controller reset operation requested in invalid state %d\n",
         sci_base_state_machine_get_state(
            scic_sds_controller_get_base_state_machine(this_controller))
      ));
   }

   return status;
}

// ---------------------------------------------------------------------------

SCI_STATUS scic_controller_get_handler_methods(
   SCIC_INTERRUPT_TYPE                interrupt_type,
   U16                                message_count,
   SCIC_CONTROLLER_HANDLER_METHODS_T *handler_methods
)
{
   SCI_STATUS status = SCI_FAILURE_UNSUPPORTED_MESSAGE_COUNT;

   switch (interrupt_type)
   {
#if !defined(DISABLE_INTERRUPTS)
   case SCIC_LEGACY_LINE_INTERRUPT_TYPE:
      if (message_count == 0)
      {
         handler_methods[0].interrupt_handler
            = scic_sds_controller_legacy_interrupt_handler;
         handler_methods[0].completion_handler
            = scic_sds_controller_legacy_completion_handler;

         status = SCI_SUCCESS;
      }
      break;

   case SCIC_MSIX_INTERRUPT_TYPE:
      if (message_count == 1)
      {
         handler_methods[0].interrupt_handler
            = scic_sds_controller_single_vector_interrupt_handler;
         handler_methods[0].completion_handler
            = scic_sds_controller_single_vector_completion_handler;

         status = SCI_SUCCESS;
      }
      else if (message_count == 2)
      {
         handler_methods[0].interrupt_handler
            = scic_sds_controller_normal_vector_interrupt_handler;
         handler_methods[0].completion_handler
            = scic_sds_controller_normal_vector_completion_handler;

         handler_methods[1].interrupt_handler
            = scic_sds_controller_error_vector_interrupt_handler;
         handler_methods[1].completion_handler
            = scic_sds_controller_error_vector_completion_handler;

         status = SCI_SUCCESS;
      }
      break;
#endif // !defined(DISABLE_INTERRUPTS)

   case SCIC_NO_INTERRUPTS:
      if (message_count == 0)
      {

         handler_methods[0].interrupt_handler
            = scic_sds_controller_polling_interrupt_handler;
         handler_methods[0].completion_handler
            = scic_sds_controller_polling_completion_handler;

         status = SCI_SUCCESS;
      }
      break;

   default:
      status = SCI_FAILURE_INVALID_PARAMETER_VALUE;
      break;
   }

   return status;
}

// ---------------------------------------------------------------------------

SCI_IO_STATUS scic_controller_start_io(
   SCI_CONTROLLER_HANDLE_T    controller,
   SCI_REMOTE_DEVICE_HANDLE_T remote_device,
   SCI_IO_REQUEST_HANDLE_T    io_request,
   U16                        io_tag
)
{
   SCI_STATUS          status;
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER,
      "scic_controller_start_io(0x%x, 0x%x, 0x%x, 0x%x) enter\n",
      controller, remote_device, io_request, io_tag
   ));

   status = this_controller->state_handlers->parent.start_io_handler(
               &this_controller->parent,
               (SCI_BASE_REMOTE_DEVICE_T *)remote_device,
               (SCI_BASE_REQUEST_T *)io_request,
               io_tag
            );

   return (SCI_IO_STATUS)status;
}

// ---------------------------------------------------------------------------

SCI_STATUS scic_controller_terminate_request(
   SCI_CONTROLLER_HANDLE_T    controller,
   SCI_REMOTE_DEVICE_HANDLE_T remote_device,
   SCI_IO_REQUEST_HANDLE_T    request
)
{
   SCI_STATUS status;
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER,
      "scic_controller_terminate_request(0x%x, 0x%x, 0x%x) enter\n",
      controller, remote_device, request
   ));

   status = this_controller->state_handlers->terminate_request_handler(
      &this_controller->parent,
      (SCI_BASE_REMOTE_DEVICE_T *)remote_device,
      (SCI_BASE_REQUEST_T *)request
   );

   return status;
}

// ---------------------------------------------------------------------------

SCI_STATUS scic_controller_complete_io(
   SCI_CONTROLLER_HANDLE_T controller,
   SCI_REMOTE_DEVICE_HANDLE_T remote_device,
   SCI_IO_REQUEST_HANDLE_T io_request
)
{
   SCI_STATUS status;
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER,
      "scic_controller_complete_io(0x%x, 0x%x, 0x%x) enter\n",
      controller, remote_device, io_request
   ));

   status = this_controller->state_handlers->parent.complete_io_handler(
      &this_controller->parent,
      (SCI_BASE_REMOTE_DEVICE_T *)remote_device,
      (SCI_BASE_REQUEST_T *)io_request
   );

   return status;
}

// ---------------------------------------------------------------------------

#if !defined(DISABLE_TASK_MANAGEMENT)

SCI_TASK_STATUS scic_controller_start_task(
   SCI_CONTROLLER_HANDLE_T    controller,
   SCI_REMOTE_DEVICE_HANDLE_T remote_device,
   SCI_TASK_REQUEST_HANDLE_T  task_request,
   U16                        task_tag
)
{
   SCI_STATUS             status = SCI_FAILURE_INVALID_STATE;
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER,
      "scic_controller_start_task(0x%x, 0x%x, 0x%x, 0x%x) enter\n",
      controller, remote_device, task_request, task_tag
   ));

   if (this_controller->state_handlers->parent.start_task_handler != NULL)
   {
      status = this_controller->state_handlers->parent.start_task_handler(
                  &this_controller->parent,
                  (SCI_BASE_REMOTE_DEVICE_T *)remote_device,
                  (SCI_BASE_REQUEST_T *)task_request,
                  task_tag
               );
   }
   else
   {
      SCIC_LOG_INFO((
         sci_base_object_get_logger(controller),
         SCIC_LOG_OBJECT_CONTROLLER,
         "SCIC Controller starting task from invalid state\n"
      ));
   }

   return (SCI_TASK_STATUS)status;
}

// ---------------------------------------------------------------------------

SCI_STATUS scic_controller_complete_task(
   SCI_CONTROLLER_HANDLE_T    controller,
   SCI_REMOTE_DEVICE_HANDLE_T remote_device,
   SCI_TASK_REQUEST_HANDLE_T  task_request
)
{
   SCI_STATUS status = SCI_FAILURE_INVALID_STATE;
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER,
      "scic_controller_complete_task(0x%x, 0x%x, 0x%x) enter\n",
      controller, remote_device, task_request
   ));

   if (this_controller->state_handlers->parent.complete_task_handler != NULL)
   {
      status = this_controller->state_handlers->parent.complete_task_handler(
                  &this_controller->parent,
                  (SCI_BASE_REMOTE_DEVICE_T *)remote_device,
                  (SCI_BASE_REQUEST_T *)task_request
               );
   }
   else
   {
      SCIC_LOG_INFO((
         sci_base_object_get_logger(controller),
         SCIC_LOG_OBJECT_CONTROLLER,
         "SCIC Controller completing task from invalid state\n"
      ));
   }

   return status;
}

#endif // !defined(DISABLE_TASK_MANAGEMENT)

// ---------------------------------------------------------------------------

SCI_STATUS scic_controller_get_port_handle(
   SCI_CONTROLLER_HANDLE_T controller,
   U8                      port_index,
   SCI_PORT_HANDLE_T *     port_handle
)
{
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER,
      "scic_controller_get_port_handle(0x%x, 0x%x, 0x%x) enter\n",
      controller, port_index, port_handle
   ));

   if (port_index < this_controller->logical_port_entries)
   {
      *port_handle = &this_controller->port_table[port_index];

      return SCI_SUCCESS;
   }

   return SCI_FAILURE_INVALID_PORT;
}

// ---------------------------------------------------------------------------

SCI_STATUS scic_controller_get_phy_handle(
   SCI_CONTROLLER_HANDLE_T controller,
   U8                      phy_index,
   SCI_PHY_HANDLE_T *      phy_handle
)
{
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER,
      "scic_controller_get_phy_handle(0x%x, 0x%x, 0x%x) enter\n",
      controller, phy_index, phy_handle
   ));

   if (phy_index < ARRAY_SIZE(this_controller->phy_table))
   {
      *phy_handle = &this_controller->phy_table[phy_index];

      return SCI_SUCCESS;
   }

   SCIC_LOG_ERROR((
      sci_base_object_get_logger(this_controller),
      SCIC_LOG_OBJECT_PORT | SCIC_LOG_OBJECT_CONTROLLER,
      "Controller:0x%x PhyId:0x%x invalid phy index\n",
      this_controller, phy_index
   ));

   return SCI_FAILURE_INVALID_PHY;
}

// ---------------------------------------------------------------------------

U16 scic_controller_allocate_io_tag(
   SCI_CONTROLLER_HANDLE_T controller
)
{
   U16 task_context;
   U16 sequence_count;
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER,
      "scic_controller_allocate_io_tag(0x%x) enter\n",
      controller
   ));

   if (!sci_pool_empty(this_controller->tci_pool))
   {
      sci_pool_get(this_controller->tci_pool, task_context);

      sequence_count = this_controller->io_request_sequence[task_context];

      return scic_sds_io_tag_construct(sequence_count, task_context);
   }

   return SCI_CONTROLLER_INVALID_IO_TAG;
}

// ---------------------------------------------------------------------------

SCI_STATUS scic_controller_free_io_tag(
   SCI_CONTROLLER_HANDLE_T controller,
   U16                     io_tag
)
{
   U16 sequence;
   U16 index;

   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   ASSERT(io_tag != SCI_CONTROLLER_INVALID_IO_TAG);

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER,
      "scic_controller_free_io_tag(0x%x, 0x%x) enter\n",
      controller, io_tag
   ));

   sequence = scic_sds_io_tag_get_sequence(io_tag);
   index    = scic_sds_io_tag_get_index(io_tag);

   if (!sci_pool_full(this_controller->tci_pool))
   {
      if (sequence == this_controller->io_request_sequence[index])
      {
         scic_sds_io_sequence_increment(
            this_controller->io_request_sequence[index]);

         sci_pool_put(this_controller->tci_pool, index);

         return SCI_SUCCESS;
      }
   }

   return SCI_FAILURE_INVALID_IO_TAG;
}

// ---------------------------------------------------------------------------

void scic_controller_enable_interrupts(
   SCI_CONTROLLER_HANDLE_T controller
)
{
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   ASSERT(this_controller->smu_registers != NULL);

   SMU_IMR_WRITE(this_controller, 0x00000000);
}

// ---------------------------------------------------------------------------

void scic_controller_disable_interrupts(
   SCI_CONTROLLER_HANDLE_T controller
)
{
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   ASSERT(this_controller->smu_registers != NULL);

   SMU_IMR_WRITE(this_controller, 0xffffffff);
}

// ---------------------------------------------------------------------------

SCI_STATUS scic_controller_set_mode(
   SCI_CONTROLLER_HANDLE_T   controller,
   SCI_CONTROLLER_MODE       operating_mode
)
{
   SCIC_SDS_CONTROLLER_T *this_controller = (SCIC_SDS_CONTROLLER_T*)controller;
   SCI_STATUS             status          = SCI_SUCCESS;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER,
      "scic_controller_set_mode(0x%x, 0x%x) enter\n",
      controller, operating_mode
   ));

   if (
         (this_controller->parent.state_machine.current_state_id
          == SCI_BASE_CONTROLLER_STATE_INITIALIZING)
      || (this_controller->parent.state_machine.current_state_id
          == SCI_BASE_CONTROLLER_STATE_INITIALIZED)
      )
   {
      switch (operating_mode)
      {
      case SCI_MODE_SPEED:
         this_controller->remote_node_entries =
            MIN(this_controller->remote_node_entries, SCI_MAX_REMOTE_DEVICES);
         this_controller->task_context_entries =
            MIN(this_controller->task_context_entries, SCU_IO_REQUEST_COUNT);
         this_controller->uf_control.buffers.count =
            MIN(this_controller->uf_control.buffers.count, SCU_UNSOLICITED_FRAME_COUNT);
         this_controller->completion_event_entries =
            MIN(this_controller->completion_event_entries, SCU_EVENT_COUNT);
         this_controller->completion_queue_entries =
            MIN(this_controller->completion_queue_entries, SCU_COMPLETION_QUEUE_COUNT);

         scic_sds_controller_build_memory_descriptor_table(this_controller);
      break;

      case SCI_MODE_SIZE:
         this_controller->remote_node_entries =
            MIN(this_controller->remote_node_entries, SCI_MIN_REMOTE_DEVICES);
         this_controller->task_context_entries =
            MIN(this_controller->task_context_entries, SCI_MIN_IO_REQUESTS);
         this_controller->uf_control.buffers.count =
            MIN(this_controller->uf_control.buffers.count, SCU_MIN_UNSOLICITED_FRAMES);
         this_controller->completion_event_entries =
            MIN(this_controller->completion_event_entries, SCU_MIN_EVENTS);
         this_controller->completion_queue_entries =
            MIN(this_controller->completion_queue_entries, SCU_MIN_COMPLETION_QUEUE_ENTRIES);

         scic_sds_controller_build_memory_descriptor_table(this_controller);
      break;

      default:
         status = SCI_FAILURE_INVALID_PARAMETER_VALUE;
      break;
      }
   }
   else
      status = SCI_FAILURE_INVALID_STATE;

   return status;
}

/**
 * This method will reset the controller hardware.
 *
 * @param[in] this_controller The controller that is to be reset.
 */
void scic_sds_controller_reset_hardware(
   SCIC_SDS_CONTROLLER_T * this_controller
)
{
   // Disable interrupts so we dont take any spurious interrupts
   scic_controller_disable_interrupts(this_controller);

   // Reset the SCU
   SMU_SMUSRCR_WRITE(this_controller, 0xFFFFFFFF);

   // Delay for 1ms to before clearing the CQP and UFQPR.
   scic_cb_stall_execution(1000);

   // The write to the CQGR clears the CQP
   SMU_CQGR_WRITE(this_controller, 0x00000000);

   // The write to the UFQGP clears the UFQPR
   SCU_UFQGP_WRITE(this_controller, 0x00000000);
}

// ---------------------------------------------------------------------------

SCI_STATUS scic_user_parameters_set(
   SCI_CONTROLLER_HANDLE_T   controller,
   SCIC_USER_PARAMETERS_T  * scic_parms
)
{
   SCIC_SDS_CONTROLLER_T * this_controller = (SCIC_SDS_CONTROLLER_T*)controller;

   if (
         (this_controller->parent.state_machine.current_state_id
          == SCI_BASE_CONTROLLER_STATE_RESET)
      || (this_controller->parent.state_machine.current_state_id
          == SCI_BASE_CONTROLLER_STATE_INITIALIZING)
      || (this_controller->parent.state_machine.current_state_id
          == SCI_BASE_CONTROLLER_STATE_INITIALIZED)
      )
   {
      U16  index;

      // Validate the user parameters.  If they are not legal, then
      // return a failure.
      for (index = 0; index < SCI_MAX_PHYS; index++)
      {
         if (!
               (  scic_parms->sds1.phys[index].max_speed_generation
                  <= SCIC_SDS_PARM_MAX_SPEED
               && scic_parms->sds1.phys[index].max_speed_generation
                  > SCIC_SDS_PARM_NO_SPEED
               )
            )
            return SCI_FAILURE_INVALID_PARAMETER_VALUE;

         if (
               (scic_parms->sds1.phys[index].in_connection_align_insertion_frequency < 3) ||
               (scic_parms->sds1.phys[index].align_insertion_frequency == 0) ||
               (scic_parms->sds1.phys[index].notify_enable_spin_up_insertion_frequency == 0)
            )
         {
            return SCI_FAILURE_INVALID_PARAMETER_VALUE;
         }
      }

      if (
            (scic_parms->sds1.stp_inactivity_timeout == 0) ||
            (scic_parms->sds1.ssp_inactivity_timeout == 0) ||
            (scic_parms->sds1.stp_max_occupancy_timeout == 0) ||
            (scic_parms->sds1.ssp_max_occupancy_timeout == 0) ||
            (scic_parms->sds1.no_outbound_task_timeout == 0)
         )
      {
         return SCI_FAILURE_INVALID_PARAMETER_VALUE;
      }

      memcpy(
         (&this_controller->user_parameters), scic_parms, sizeof(*scic_parms));

      return SCI_SUCCESS;
   }

   return SCI_FAILURE_INVALID_STATE;
}

// ---------------------------------------------------------------------------

void scic_user_parameters_get(
   SCI_CONTROLLER_HANDLE_T   controller,
   SCIC_USER_PARAMETERS_T   * scic_parms
)
{
   SCIC_SDS_CONTROLLER_T * this_controller = (SCIC_SDS_CONTROLLER_T*)controller;

   memcpy(scic_parms, (&this_controller->user_parameters), sizeof(*scic_parms));
}

// ---------------------------------------------------------------------------
SCI_STATUS scic_oem_parameters_set(
   SCI_CONTROLLER_HANDLE_T   controller,
   SCIC_OEM_PARAMETERS_T   * scic_parms,
   U8 scic_parms_version
)
{
   SCIC_SDS_CONTROLLER_T * this_controller = (SCIC_SDS_CONTROLLER_T*)controller;
   SCI_BIOS_OEM_PARAM_ELEMENT_T *old_oem_params =
                (SCI_BIOS_OEM_PARAM_ELEMENT_T *)(&(scic_parms->sds1));


   if (
         (this_controller->parent.state_machine.current_state_id
          == SCI_BASE_CONTROLLER_STATE_RESET)
      || (this_controller->parent.state_machine.current_state_id
          == SCI_BASE_CONTROLLER_STATE_INITIALIZING)
      || (this_controller->parent.state_machine.current_state_id
          == SCI_BASE_CONTROLLER_STATE_INITIALIZED)
      )
   {
      U16  index;
      U8   combined_phy_mask = 0;

      /*
       * Set the OEM parameter version for the controller. This comes
       * from the OEM parameter block header or the registry depending
       * on what WCDL is set to retrieve.
       */
      this_controller->oem_parameters_version = scic_parms_version;

      // Validate the oem parameters.  If they are not legal, then
      // return a failure.
      for(index=0; index<SCI_MAX_PORTS; index++)
      {
         if (scic_parms->sds1.ports[index].phy_mask > SCIC_SDS_PARM_PHY_MASK_MAX)
         {
            return SCI_FAILURE_INVALID_PARAMETER_VALUE;
         }
      }

      for(index=0; index<SCI_MAX_PHYS; index++)
      {
         if (
             scic_parms->sds1.phys[index].sas_address.sci_format.high == 0
                 && scic_parms->sds1.phys[index].sas_address.sci_format.low  == 0
        )
        {
            return SCI_FAILURE_INVALID_PARAMETER_VALUE;
        }

#if defined(PBG_HBA_A0_BUILD) || defined(PBG_HBA_A2_BUILD) || defined(PBG_HBA_BETA_BUILD) || defined(PBG_BUILD)
        if (
              (scic_parms->sds1.phys[index].afe_tx_amp_control0 == 0) ||
              (scic_parms->sds1.phys[index].afe_tx_amp_control1 == 0) ||
              (scic_parms->sds1.phys[index].afe_tx_amp_control2 == 0) ||
              (scic_parms->sds1.phys[index].afe_tx_amp_control3 == 0)
              )
        {
           return SCI_FAILURE_INVALID_PARAMETER_VALUE;
        }
#endif
      }

      if (scic_parms->sds1.controller.mode_type == SCIC_PORT_AUTOMATIC_CONFIGURATION_MODE)
      {
         for(index=0; index<SCI_MAX_PHYS; index++)
         {
            if (scic_parms->sds1.ports[index].phy_mask != 0)
            {
               return SCI_FAILURE_INVALID_PARAMETER_VALUE;
            }
         }
      }
      else if (scic_parms->sds1.controller.mode_type == SCIC_PORT_MANUAL_CONFIGURATION_MODE)
      {
         for(index=0; index<SCI_MAX_PHYS; index++)
         {
            combined_phy_mask |= scic_parms->sds1.ports[index].phy_mask;
         }

         if (combined_phy_mask == 0)
         {
            return SCI_FAILURE_INVALID_PARAMETER_VALUE;
         }
      }
      else
      {
         return SCI_FAILURE_INVALID_PARAMETER_VALUE;
      }

      if (scic_parms->sds1.controller.max_number_concurrent_device_spin_up > MAX_CONCURRENT_DEVICE_SPIN_UP_COUNT)
      {
         return SCI_FAILURE_INVALID_PARAMETER_VALUE;
      }

      if (old_oem_params->controller.do_enable_ssc != 0)
      {
         if (  (scic_parms_version == SCI_OEM_PARAM_VER_1_0)
            && (old_oem_params->controller.do_enable_ssc != 0x01))
             return SCI_FAILURE_INVALID_PARAMETER_VALUE;

         if (scic_parms_version >= SCI_OEM_PARAM_VER_1_1)
         {
            SCI_BIOS_OEM_PARAM_ELEMENT_v_1_1_T *oem_params =
                (SCI_BIOS_OEM_PARAM_ELEMENT_v_1_1_T*)(&(scic_parms->sds1));

            U8 test = oem_params->controller.ssc_sata_tx_spread_level;
            if ( !((test == 0x0) || (test == 0x2) || (test == 0x3) ||
                 (test == 0x6) || (test == 0x7)) )
                return SCI_FAILURE_INVALID_PARAMETER_VALUE;

            test = oem_params->controller.ssc_sas_tx_spread_level;
            if (oem_params->controller.ssc_sas_tx_type == 0)
            {
                if ( !((test == 0x0) || (test == 0x2) || (test == 0x3)) )
                    return SCI_FAILURE_INVALID_PARAMETER_VALUE;
            }
            else
            if (oem_params->controller.ssc_sas_tx_type == 1)
            {
                if ( !((test == 0x0) || (test == 0x3) || (test == 0x6)) )
                    return SCI_FAILURE_INVALID_PARAMETER_VALUE;
            }
         }
      }

      memcpy(
         (&this_controller->oem_parameters), scic_parms, sizeof(*scic_parms));
      return SCI_SUCCESS;
   }

   return SCI_FAILURE_INVALID_STATE;
}

// ---------------------------------------------------------------------------

void scic_oem_parameters_get(
   SCI_CONTROLLER_HANDLE_T   controller,
   SCIC_OEM_PARAMETERS_T   * scic_parms
)
{
   SCIC_SDS_CONTROLLER_T * this_controller = (SCIC_SDS_CONTROLLER_T*)controller;

   memcpy(scic_parms, (&this_controller->oem_parameters), sizeof(*scic_parms));
}

// ---------------------------------------------------------------------------

#if !defined(DISABLE_INTERRUPTS)

#define INTERRUPT_COALESCE_TIMEOUT_BASE_RANGE_LOWER_BOUND_NS 853
#define INTERRUPT_COALESCE_TIMEOUT_BASE_RANGE_UPPER_BOUND_NS 1280
#define INTERRUPT_COALESCE_TIMEOUT_MAX_US                    2700000
#define INTERRUPT_COALESCE_NUMBER_MAX                        256
#define INTERRUPT_COALESCE_TIMEOUT_ENCODE_MIN                7
#define INTERRUPT_COALESCE_TIMEOUT_ENCODE_MAX                28

SCI_STATUS scic_controller_set_interrupt_coalescence(
   SCI_CONTROLLER_HANDLE_T controller,
   U32                     coalesce_number,
   U32                     coalesce_timeout
)
{
   SCIC_SDS_CONTROLLER_T * scic_controller = (SCIC_SDS_CONTROLLER_T *)controller;
   U8 timeout_encode = 0;
   U32 min = 0;
   U32 max = 0;

   //Check if the input parameters fall in the range.
   if (coalesce_number > INTERRUPT_COALESCE_NUMBER_MAX)
      return SCI_FAILURE_INVALID_PARAMETER_VALUE;

   //  Defined encoding for interrupt coalescing timeout:
   //              Value   Min      Max     Units
   //              -----   ---      ---     -----
   //              0       -        -       Disabled
   //              1       13.3     20.0    ns
   //              2       26.7     40.0
   //              3       53.3     80.0
   //              4       106.7    160.0
   //              5       213.3    320.0
   //              6       426.7    640.0
   //              7       853.3    1280.0
   //              8       1.7      2.6     us
   //              9       3.4      5.1
   //              10      6.8      10.2
   //              11      13.7     20.5
   //              12      27.3     41.0
   //              13      54.6     81.9
   //              14      109.2    163.8
   //              15      218.5    327.7
   //              16      436.9    655.4
   //              17      873.8    1310.7
   //              18      1.7      2.6     ms
   //              19      3.5      5.2
   //              20      7.0      10.5
   //              21      14.0     21.0
   //              22      28.0     41.9
   //              23      55.9     83.9
   //              24      111.8    167.8
   //              25      223.7    335.5
   //              26      447.4    671.1
   //              27      894.8    1342.2
   //              28      1.8      2.7     s
   //              Others Undefined

   //Use the table above to decide the encode of interrupt coalescing timeout
   //value for register writing.
   if (coalesce_timeout == 0)
      timeout_encode = 0;
   else
   {
      //make the timeout value in unit of (10 ns).
      coalesce_timeout = coalesce_timeout * 100;
      min = INTERRUPT_COALESCE_TIMEOUT_BASE_RANGE_LOWER_BOUND_NS / 10;
      max = INTERRUPT_COALESCE_TIMEOUT_BASE_RANGE_UPPER_BOUND_NS / 10;

      //get the encode of timeout for register writing.
      for ( timeout_encode = INTERRUPT_COALESCE_TIMEOUT_ENCODE_MIN;
            timeout_encode <= INTERRUPT_COALESCE_TIMEOUT_ENCODE_MAX;
            timeout_encode++ )
      {
         if (min <= coalesce_timeout &&  max > coalesce_timeout)
            break;
         else if (coalesce_timeout >= max && coalesce_timeout < min*2
            && coalesce_timeout <= INTERRUPT_COALESCE_TIMEOUT_MAX_US*100)
         {
            if ( (coalesce_timeout-max) < (2*min - coalesce_timeout) )
               break;
            else
            {
               timeout_encode++;
               break;
            }
         }
         else
         {
            max = max*2;
            min = min*2;
         }
      }

      if ( timeout_encode == INTERRUPT_COALESCE_TIMEOUT_ENCODE_MAX+1 )
         //the value is out of range.
         return SCI_FAILURE_INVALID_PARAMETER_VALUE;
   }

   SMU_ICC_WRITE(
      scic_controller,
      (SMU_ICC_GEN_VAL(NUMBER, coalesce_number)|
       SMU_ICC_GEN_VAL(TIMER, timeout_encode))
   );

   scic_controller->interrupt_coalesce_number = (U16)coalesce_number;
   scic_controller->interrupt_coalesce_timeout = coalesce_timeout/100;

   return SCI_SUCCESS;
}

// ---------------------------------------------------------------------------

void scic_controller_get_interrupt_coalescence(
   SCI_CONTROLLER_HANDLE_T   controller,
   U32                     * coalesce_number,
   U32                     * coalesce_timeout
)
{
   SCIC_SDS_CONTROLLER_T * scic_controller = (SCIC_SDS_CONTROLLER_T *)controller;
   *coalesce_number = scic_controller->interrupt_coalesce_number;
   *coalesce_timeout = scic_controller->interrupt_coalesce_timeout;
}

#endif // !defined(DISABLE_INTERRUPTS)

// ---------------------------------------------------------------------------

U32 scic_controller_get_scratch_ram_size(
   SCI_CONTROLLER_HANDLE_T   controller
)
{
   return SCU_SCRATCH_RAM_SIZE_IN_DWORDS;
}

// ---------------------------------------------------------------------------

SCI_STATUS scic_controller_read_scratch_ram_dword(
   SCI_CONTROLLER_HANDLE_T   controller,
   U32                       offset,
   U32                     * value
)
{
   U32 zpt_index;
   SCIC_SDS_CONTROLLER_T * scic_controller = (SCIC_SDS_CONTROLLER_T *)controller;
   U32 status = SMU_SMUCSR_READ(scic_controller);

   //Check if the SCU Scratch RAM been initialized, if not return zeros
   if ((status & SCU_RAM_INIT_COMPLETED) != SCU_RAM_INIT_COMPLETED)
   {
      *value = 0x00000000;
      return SCI_SUCCESS;
   }

   if (offset < scic_controller_get_scratch_ram_size(controller))
   {
      if(offset <= SCU_MAX_ZPT_DWORD_INDEX)
      {
         zpt_index = offset + (offset - (offset % 4)) + 4;

         *value = scu_controller_scratch_ram_register_read(scic_controller,zpt_index);
      }
      else //offset > SCU_MAX_ZPT_DWORD_INDEX
      {
         offset = offset - 132;

         zpt_index = offset + (offset - (offset % 4)) + 4;

         *value = scu_controller_scratch_ram_register_read_ext(scic_controller,zpt_index);
      }

      return SCI_SUCCESS;
   }
   else
   {
      return SCI_FAILURE_INVALID_PARAMETER_VALUE;
   }
}

// ---------------------------------------------------------------------------

SCI_STATUS scic_controller_write_scratch_ram_dword(
   SCI_CONTROLLER_HANDLE_T   controller,
   U32                       offset,
   U32                       value
)
{
   U32 zpt_index;

   if (offset < scic_controller_get_scratch_ram_size(controller))
   {
      SCIC_SDS_CONTROLLER_T * scic_controller = (SCIC_SDS_CONTROLLER_T *)controller;

      if(offset <= SCU_MAX_ZPT_DWORD_INDEX)
      {
         zpt_index = offset + (offset - (offset % 4)) + 4;

         scu_controller_scratch_ram_register_write(scic_controller,zpt_index,value);
      }
      else //offset > SCU_MAX_ZPT_DWORD_INDEX
      {
         offset = offset - 132;

         zpt_index = offset + (offset - (offset % 4)) + 4;

         scu_controller_scratch_ram_register_write_ext(scic_controller,zpt_index,value);

      }

      return SCI_SUCCESS;
   }
   else
   {
      return SCI_FAILURE_INVALID_PARAMETER_VALUE;
   }
}

// ---------------------------------------------------------------------------

SCI_STATUS scic_controller_suspend(
   SCI_CONTROLLER_HANDLE_T   controller
)
{
   SCIC_SDS_CONTROLLER_T * this_controller = (SCIC_SDS_CONTROLLER_T*)controller;
   U8 index;

   // As a precaution, disable interrupts.  The user is required
   // to re-enable interrupts if so desired after the call.
   scic_controller_disable_interrupts(controller);

   // Stop all the timers
   // Maybe change the states of the objects to avoid processing stuff.


   // Suspend the Ports in order to ensure no unexpected
   // frame reception occurs on the links from the target
   for (index = 0; index < SCI_MAX_PORTS; index++)
      scic_sds_port_suspend_port_task_scheduler(
         &(this_controller->port_table[index]));

   // Disable/Reset the completion queue and unsolicited frame
   // queue.
   SMU_CQGR_WRITE(this_controller, 0x00000000);
   SCU_UFQGP_WRITE(this_controller, 0x00000000);

   // Clear any interrupts that may be pending or may have been generated
   // by setting CQGR and CQPR back to 0
   SMU_ISR_WRITE(this_controller, 0xFFFFFFFF);

   //reset the software get pointer to completion queue.
   this_controller->completion_queue_get = 0;

   return SCI_SUCCESS;
}

// ---------------------------------------------------------------------------

SCI_STATUS scic_controller_resume(
   SCI_CONTROLLER_HANDLE_T   controller
)
{
   SCIC_SDS_CONTROLLER_T * this_controller = (SCIC_SDS_CONTROLLER_T*)controller;
   U8 index;

   // Initialize the completion queue and unsolicited frame queue.
   scic_sds_controller_initialize_completion_queue(this_controller);
   scic_sds_controller_initialize_unsolicited_frame_queue(this_controller);

   this_controller->restrict_completions = FALSE;

   // Release the port suspensions to allow for further successful
   // operation.
   for (index = 0; index < SCI_MAX_PORTS; index++)
      scic_sds_port_resume_port_task_scheduler(
         &(this_controller->port_table[index]));

   //check the link layer status register DWORD sync acquired bit to detect
   //link down event. If there is any link down event happened during controller
   //suspension, restart phy state machine.
   for (index = 0; index < SCI_MAX_PHYS; index ++)
   {
      SCIC_SDS_PHY_T * curr_phy = &this_controller->phy_table[index];
      U32 link_layer_status = SCU_SAS_LLSTA_READ(curr_phy);

      if ((link_layer_status & SCU_SAS_LLSTA_DWORD_SYNCA_BIT) == 0)
      {
         //Need to put the phy back to start OOB. Then an appropriate link event
         //message will be send to scic user.
         scic_sds_phy_restart_starting_state(curr_phy);
      }
   }

   return SCI_SUCCESS;
}

// ---------------------------------------------------------------------------

SCI_STATUS scic_controller_transition(
   SCI_CONTROLLER_HANDLE_T   controller,
   BOOL                      restrict_completions
)
{
   SCI_STATUS              result = SCI_FAILURE_INVALID_STATE;
   SCIC_SDS_CONTROLLER_T * this_controller = (SCIC_SDS_CONTROLLER_T*)controller;
   U8                      index;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER,
      "scic_controller_transition(0x%x) enter\n",
      controller
   ));

   if (this_controller->parent.state_machine.current_state_id
       == SCI_BASE_CONTROLLER_STATE_READY)
   {
      // Ensure that there are no outstanding IO operations at this
      // time.
      for (index = 0; index < SCI_MAX_PORTS; index++)
      {
         if (this_controller->port_table[index].started_request_count != 0)
            return result;
      }

      scic_controller_suspend(controller);

      // Loop through the memory descriptor list and reprogram
      // the silicon memory registers accordingly.
      result = scic_sds_controller_validate_memory_descriptor_table(
                  this_controller);
      if (result == SCI_SUCCESS)
      {
         scic_sds_controller_ram_initialization(this_controller);
         this_controller->restrict_completions = restrict_completions;
      }

      scic_controller_resume(controller);
   }

   return result;
}

// ---------------------------------------------------------------------------

SCI_STATUS scic_controller_get_max_ports(
   SCI_CONTROLLER_HANDLE_T   controller,
   U8                      * count
)
{
   *count = SCI_MAX_PORTS;
   return SCI_SUCCESS;
}

// ---------------------------------------------------------------------------

SCI_STATUS scic_controller_get_max_phys(
   SCI_CONTROLLER_HANDLE_T   controller,
   U8                      * count
)
{
   *count = SCI_MAX_PHYS;
   return SCI_SUCCESS;
}


//******************************************************************************
//* CONTROLLER STATE MACHINE
//******************************************************************************

/**
 * This macro returns the maximum number of logical ports supported by the
 * hardware. The caller passes in the value read from the device context
 * capacity register and this macro will mash and shift the value
 * appropriately.
 */
#define smu_dcc_get_max_ports(dcc_value) \
   ( \
     (    ((U32)((dcc_value) & SMU_DEVICE_CONTEXT_CAPACITY_MAX_LP_MASK)) \
       >> SMU_DEVICE_CONTEXT_CAPACITY_MAX_LP_SHIFT ) + 1\
   )

/**
 * This macro returns the maximum number of task contexts supported by the
 * hardware. The caller passes in the value read from the device context
 * capacity register and this macro will mash and shift the value
 * appropriately.
 */
#define smu_dcc_get_max_task_context(dcc_value) \
   ( \
     (   ((U32)((dcc_value) & SMU_DEVICE_CONTEXT_CAPACITY_MAX_TC_MASK)) \
       >> SMU_DEVICE_CONTEXT_CAPACITY_MAX_TC_SHIFT ) + 1\
   )

/**
 * This macro returns the maximum number of remote node contexts supported
 * by the hardware. The caller passes in the value read from the device
 * context capacity register and this macro will mash and shift the value
 * appropriately.
 */
#define smu_dcc_get_max_remote_node_context(dcc_value) \
   ( \
     (  ( (U32)((dcc_value) & SMU_DEVICE_CONTEXT_CAPACITY_MAX_RNC_MASK) )\
       >> SMU_DEVICE_CONTEXT_CAPACITY_MAX_RNC_SHIFT ) + 1\
   )

//*****************************************************************************
//* DEFAULT STATE HANDLERS
//*****************************************************************************

/**
 * This method is called when the SCIC_SDS_CONTROLLER default start
 * io/task handler is in place.
 *    - Issue a warning message
 *
 * @param[in] controller This is SCI_BASE_CONTROLLER object which is cast
 *       into a SCIC_SDS_CONTROLLER object.
 * @param[in] remote_device This is SCI_BASE_REMOTE_DEVICE which, if it was
 *       used, would be cast to a SCIC_SDS_REMOTE_DEVICE.
 * @param[in] io_request This is the SCI_BASE_REQUEST which, if it was used,
 *       would be cast to a SCIC_SDS_IO_REQUEST.
 * @param[in] io_tag This is the IO tag to be assigned to the IO request or
 *       SCI_CONTROLLER_INVALID_IO_TAG.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_INVALID_STATE
 */
static
SCI_STATUS scic_sds_controller_default_start_operation_handler(
   SCI_BASE_CONTROLLER_T    *controller,
   SCI_BASE_REMOTE_DEVICE_T *remote_device,
   SCI_BASE_REQUEST_T       *io_request,
   U16                       io_tag
)
{
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   SCIC_LOG_WARNING((
      sci_base_object_get_logger(this_controller),
      SCIC_LOG_OBJECT_CONTROLLER,
      "SCIC Controller requested to start an io/task from invalid state %d\n",
      sci_base_state_machine_get_state(
         scic_sds_controller_get_base_state_machine(this_controller))
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * This method is called when the SCIC_SDS_CONTROLLER default
 * request handler is in place.
 *    - Issue a warning message
 *
 * @param[in] controller This is SCI_BASE_CONTROLLER object which is cast
 *       into a SCIC_SDS_CONTROLLER object.
 * @param[in] remote_device This is SCI_BASE_REMOTE_DEVICE which, if it was
 *       used, would be cast to a SCIC_SDS_REMOTE_DEVICE.
 * @param[in] io_request This is the SCI_BASE_REQUEST which, if it was used,
 *       would be cast to a SCIC_SDS_IO_REQUEST.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_INVALID_STATE
 */
static
SCI_STATUS scic_sds_controller_default_request_handler(
   SCI_BASE_CONTROLLER_T    *controller,
   SCI_BASE_REMOTE_DEVICE_T *remote_device,
   SCI_BASE_REQUEST_T       *io_request
)
{
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   SCIC_LOG_WARNING((
      sci_base_object_get_logger(this_controller),
      SCIC_LOG_OBJECT_CONTROLLER,
      "SCIC Controller request operation from invalid state %d\n",
      sci_base_state_machine_get_state(
         scic_sds_controller_get_base_state_machine(this_controller))
   ));

   return SCI_FAILURE_INVALID_STATE;
}

//*****************************************************************************
//* GENERAL (COMMON) STATE HANDLERS
//*****************************************************************************

/**
 * This method is called when the SCIC_SDS_CONTROLLER is in the ready state
 * reset handler is in place.
 *    - Transition to SCI_BASE_CONTROLLER_STATE_RESETTING
 *
 * @param[in] controller The SCI_BASE_CONTROLLER object which is cast into a
 *       SCIC_SDS_CONTROLLER object.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS
 */
static
SCI_STATUS scic_sds_controller_general_reset_handler(
   SCI_BASE_CONTROLLER_T *controller
)
{
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER,
      "scic_sds_controller_resetting_state_enter(0x%x) enter\n",
      controller
   ));

   //Release resource. So far only resource to be released are timers.
   scic_sds_controller_release_resource(this_controller);

   // The reset operation is not a graceful cleanup just perform the state
   // transition.
   sci_base_state_machine_change_state(
      scic_sds_controller_get_base_state_machine(this_controller),
      SCI_BASE_CONTROLLER_STATE_RESETTING
   );

   return SCI_SUCCESS;
}

//*****************************************************************************
//* RESET STATE HANDLERS
//*****************************************************************************

/**
 * This method is the SCIC_SDS_CONTROLLER initialize handler for the reset
 * state.
 *    - Currently this function does nothing
 *
 * @param[in] controller This is the SCI_BASE_CONTROLLER object which is cast
 *       into a SCIC_SDS_CONTROLLER object.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE
 *
 * @todo This function is not yet implemented and is a valid request from the
 *       reset state.
 */
static
SCI_STATUS scic_sds_controller_reset_state_initialize_handler(
   SCI_BASE_CONTROLLER_T *controller
)
{
   U32 index;
   SCI_STATUS result = SCI_SUCCESS;
   SCIC_SDS_CONTROLLER_T *this_controller;

   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIC_LOG_OBJECT_CONTROLLER | SCIC_LOG_OBJECT_INITIALIZATION,
      "scic_sds_controller_reset_state_initialize_handler(0x%x) enter\n",
      controller
   ));

   sci_base_state_machine_change_state(
      scic_sds_controller_get_base_state_machine(this_controller),
      SCI_BASE_CONTROLLER_STATE_INITIALIZING
   );

   this_controller->timeout_timer = scic_cb_timer_create(
      controller,
      scic_sds_controller_timeout_handler,
      controller
   );

   scic_sds_controller_initialize_power_control(this_controller);

   /// todo: This should really be done in the reset state enter but
   ///       the controller has not yet been initialized before getting
   ///       to the reset enter state so the PCI BAR is not yet assigned
   scic_sds_controller_reset_hardware(this_controller);

#if defined(ARLINGTON_BUILD)
   scic_sds_controller_lex_atux_initialization(this_controller);
#elif    defined(PLEASANT_RIDGE_BUILD) \
      || defined(PBG_HBA_A0_BUILD) \
      || defined(PBG_HBA_A2_BUILD)
   scic_sds_controller_afe_initialization(this_controller);
#elif defined(PBG_HBA_BETA_BUILD) || defined(PBG_BUILD)
   // There is nothing to do here for B0 since we do not have to
   // program the AFE registers.
   /// @todo The AFE settings are supposed to be correct for the B0 but
   ///       presently they seem to be wrong.
   scic_sds_controller_afe_initialization(this_controller);
#else  // !defined(ARLINGTON_BUILD) && !defined(PLEASANT_RIDGE_BUILD)
   // What other systems do we want to add here?
#endif // !defined(ARLINGTON_BUILD) && !defined(PLEASANT_RIDGE_BUILD)

   if (SCI_SUCCESS == result)
   {
      U32 status;
      U32 terminate_loop;

      // Take the hardware out of reset
      SMU_SMUSRCR_WRITE(this_controller, 0x00000000);

      /// @todo Provide meaningfull error code for hardware failure
      //result = SCI_FAILURE_CONTROLLER_HARDWARE;
      result = SCI_FAILURE;
      terminate_loop = 100;

      while (terminate_loop-- && (result != SCI_SUCCESS))
      {
         // Loop until the hardware reports success
         scic_cb_stall_execution(SCU_CONTEXT_RAM_INIT_STALL_TIME);
         status = SMU_SMUCSR_READ(this_controller);

         if ((status & SCU_RAM_INIT_COMPLETED) == SCU_RAM_INIT_COMPLETED)
         {
            result = SCI_SUCCESS;
         }
      }
   }

#ifdef ARLINGTON_BUILD
   scic_sds_controller_enable_chipwatch(this_controller);
#endif

   if (result == SCI_SUCCESS)
   {
      U32 max_supported_ports;
      U32 max_supported_devices;
      U32 max_supported_io_requests;
      U32 device_context_capacity;

      // Determine what are the actaul device capacities that the
      // hardware will support
      device_context_capacity = SMU_DCC_READ(this_controller);

      max_supported_ports =
         smu_dcc_get_max_ports(device_context_capacity);
      max_supported_devices =
         smu_dcc_get_max_remote_node_context(device_context_capacity);
      max_supported_io_requests =
         smu_dcc_get_max_task_context(device_context_capacity);

      // Make all PEs that are unassigned match up with the logical ports
      for (index = 0; index < max_supported_ports; index++)
      {
         scu_register_write(
            this_controller,
            this_controller->scu_registers->peg0.ptsg.protocol_engine[index],
            index
         );
      }

      // Now that we have the correct hardware reported minimum values
      // build the MDL for the controller.  Default to a performance
      // configuration.
      scic_controller_set_mode(this_controller, SCI_MODE_SPEED);

      // Record the smaller of the two capacity values
      this_controller->logical_port_entries =
         MIN(max_supported_ports, this_controller->logical_port_entries);

      this_controller->task_context_entries =
         MIN(max_supported_io_requests, this_controller->task_context_entries);

      this_controller->remote_node_entries =
         MIN(max_supported_devices, this_controller->remote_node_entries);
   }

   // Initialize hardware PCI Relaxed ordering in DMA engines
   if (result == SCI_SUCCESS)
   {
      U32 dma_configuration;

      // Configure the payload DMA
      dma_configuration = SCU_PDMACR_READ(this_controller);
      dma_configuration |= SCU_PDMACR_GEN_BIT(PCI_RELAXED_ORDERING_ENABLE);
      SCU_PDMACR_WRITE(this_controller, dma_configuration);

      // Configure the control DMA
      dma_configuration = SCU_CDMACR_READ(this_controller);
      dma_configuration |= SCU_CDMACR_GEN_BIT(PCI_RELAXED_ORDERING_ENABLE);
      SCU_CDMACR_WRITE(this_controller, dma_configuration);
   }

   // Initialize the PHYs before the PORTs because the PHY registers
   // are accessed during the port initialization.
   if (result == SCI_SUCCESS)
   {
      // Initialize the phys
      for (index = 0;
           (result == SCI_SUCCESS) && (index < SCI_MAX_PHYS);
           index++)
      {
         result = scic_sds_phy_initialize(
            &this_controller->phy_table[index],
            &this_controller->scu_registers->peg0.pe[index].tl,
            &this_controller->scu_registers->peg0.pe[index].ll
         );
      }
   }

   //Initialize the SGPIO Unit for HARDWARE controlled SGPIO
   if(result == SCI_SUCCESS)
   {
      scic_sgpio_hardware_initialize(this_controller);
   }

   if (result == SCI_SUCCESS)
   {
      // Initialize the logical ports
      for (index = 0;
              (index < this_controller->logical_port_entries)
           && (result == SCI_SUCCESS);
           index++)
      {
         result = scic_sds_port_initialize(
            &this_controller->port_table[index],
            &this_controller->scu_registers->peg0.ptsg.port[index],
            &this_controller->scu_registers->peg0.ptsg.protocol_engine,
            &this_controller->scu_registers->peg0.viit[index]
         );
      }
   }

   if (SCI_SUCCESS == result)
   {
      result = scic_sds_port_configuration_agent_initialize(
                  this_controller,
                  &this_controller->port_agent
               );
   }

   // Advance the controller state machine
   if (result == SCI_SUCCESS)
   {
      sci_base_state_machine_change_state(
         scic_sds_controller_get_base_state_machine(this_controller),
         SCI_BASE_CONTROLLER_STATE_INITIALIZED
      );
   }
   else
   {
      //stay in the same state and release the resource
      scic_sds_controller_release_resource(this_controller);

      SCIC_LOG_TRACE((
         sci_base_object_get_logger(controller),
         SCIC_LOG_OBJECT_CONTROLLER | SCIC_LOG_OBJECT_INITIALIZATION,
         "Invalid Port Configuration from scic_sds_controller_reset_state_initialize_handler(0x%x) \n",
         controller
      ));

   }

   return result;
}

//*****************************************************************************
//* INITIALIZED STATE HANDLERS
//*****************************************************************************

/**
 * This method is the SCIC_SDS_CONTROLLER start handler for the initialized
 * state.
 *    - Validate we have a good memory descriptor table
 *    - Initialze the physical memory before programming the hardware
 *    - Program the SCU hardware with the physical memory addresses passed in
 *      the memory descriptor table.
 *    - Initialzie the TCi pool
 *    - Initialize the RNi pool
 *    - Initialize the completion queue
 *    - Initialize the unsolicited frame data
 *    - Take the SCU port task scheduler out of reset
 *    - Start the first phy object.
 *    - Transition to SCI_BASE_CONTROLLER_STATE_STARTING.
 *
 * @param[in] controller This is the SCI_BASE_CONTROLLER object which is cast
 *       into a SCIC_SDS_CONTROLLER object.
 * @param[in] timeout This is the allowed time for the controller object to
 *       reach the started state.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS if all of the controller start operations complete
 * @retval SCI_FAILURE_UNSUPPORTED_INFORMATION_FIELD if one or more of the
 *         memory descriptor fields is invalid.
 */
static
SCI_STATUS scic_sds_controller_initialized_state_start_handler(
   SCI_BASE_CONTROLLER_T * controller,
   U32                     timeout
)
{
   U16                     index;
   SCI_STATUS              result;
   SCIC_SDS_CONTROLLER_T * this_controller;

   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   // Make sure that the SCI User filled in the memory descriptor table correctly
   result = scic_sds_controller_validate_memory_descriptor_table(this_controller);

   if (result == SCI_SUCCESS)
   {
      // The memory descriptor list looks good so program the hardware
      scic_sds_controller_ram_initialization(this_controller);
   }

   if (SCI_SUCCESS == result)
   {
      // Build the TCi free pool
      sci_pool_initialize(this_controller->tci_pool);
      for (index = 0; index < this_controller->task_context_entries; index++)
      {
         sci_pool_put(this_controller->tci_pool, index);
      }

      // Build the RNi free pool
      scic_sds_remote_node_table_initialize(
         &this_controller->available_remote_nodes,
         this_controller->remote_node_entries
      );
   }

   if (SCI_SUCCESS == result)
   {
      // Before anything else lets make sure we will not be interrupted
      // by the hardware.
      scic_controller_disable_interrupts(controller);

      // Enable the port task scheduler
      scic_sds_controller_enable_port_task_scheduler(this_controller);

      // Assign all the task entries to this controller physical function
      scic_sds_controller_assign_task_entries(this_controller);

      // Now initialze the completion queue
      scic_sds_controller_initialize_completion_queue(this_controller);

      // Initialize the unsolicited frame queue for use
      scic_sds_controller_initialize_unsolicited_frame_queue(this_controller);

      // Setup the phy start timer
      result = scic_sds_controller_initialize_phy_startup(this_controller);
   }

   // Start all of the ports on this controller
   for (
          index = 0;
          (index < this_controller->logical_port_entries) && (result == SCI_SUCCESS);
          index++
       )
   {
      result = this_controller->port_table[index].
         state_handlers->parent.start_handler(&this_controller->port_table[index].parent);
   }

   if (SCI_SUCCESS == result)
   {
      scic_sds_controller_start_next_phy(this_controller);

      // See if the user requested to timeout this operation.
      if (timeout != 0)
         scic_cb_timer_start(controller, this_controller->timeout_timer, timeout);

      sci_base_state_machine_change_state(
         scic_sds_controller_get_base_state_machine(this_controller),
         SCI_BASE_CONTROLLER_STATE_STARTING
      );
   }

   return result;
}

//*****************************************************************************
//* STARTING STATE HANDLERS
//*****************************************************************************

/**
 * This method is called when the SCIC_SDS_CONTROLLER is in the starting state
 * link up handler is called.  This method will perform the following:
 *    - Stop the phy timer
 *    - Start the next phy
 *    - Report the link up condition to the port object
 *
 * @param[in] controller This is SCIC_SDS_CONTROLLER which receives the link up
 *       notification.
 * @param[in] port This is SCIC_SDS_PORT with which the phy is associated.
 * @param[in] phy This is the SCIC_SDS_PHY which has gone link up.
 *
 * @return none
 */
static
void scic_sds_controller_starting_state_link_up_handler(
   SCIC_SDS_CONTROLLER_T *this_controller,
   SCIC_SDS_PORT_T       *port,
   SCIC_SDS_PHY_T        *phy
)
{
   scic_sds_controller_phy_timer_stop(this_controller);

   this_controller->port_agent.link_up_handler(
      this_controller, &this_controller->port_agent, port, phy
   );
   //scic_sds_port_link_up(port, phy);

   scic_sds_controller_start_next_phy(this_controller);
}

/**
 * This method is called when the SCIC_SDS_CONTROLLER is in the starting state
 * link down handler is called.
 *    - Report the link down condition to the port object
 *
 * @param[in] controller This is SCIC_SDS_CONTROLLER which receives the
 *       link down notification.
 * @param[in] port This is SCIC_SDS_PORT with which the phy is associated.
 * @param[in] phy This is the SCIC_SDS_PHY which has gone link down.
 *
 * @return none
 */
static
void scic_sds_controller_starting_state_link_down_handler(
   SCIC_SDS_CONTROLLER_T *this_controller,
   SCIC_SDS_PORT_T       *port,
   SCIC_SDS_PHY_T        *phy
)
{
   this_controller->port_agent.link_down_handler(
      this_controller, &this_controller->port_agent, port, phy
   );
   //scic_sds_port_link_down(port, phy);
}

//*****************************************************************************
//* READY STATE HANDLERS
//*****************************************************************************

/**
 * This method is called when the SCIC_SDS_CONTROLLER is in the ready state
 * stop handler is called.
 *    - Start the timeout timer
 *    - Transition to SCI_BASE_CONTROLLER_STATE_STOPPING.
 *
 * @param[in] controller The SCI_BASE_CONTROLLER object which is cast into a
 *       SCIC_SDS_CONTROLLER object.
 * @param[in] timeout The timeout for when the stop operation should report a
 *       failure.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS
 */
static
SCI_STATUS scic_sds_controller_ready_state_stop_handler(
   SCI_BASE_CONTROLLER_T *controller,
   U32                   timeout
)
{
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   // See if the user requested to timeout this operation
   if (timeout != 0)
      scic_cb_timer_start(controller, this_controller->timeout_timer, timeout);

   sci_base_state_machine_change_state(
      scic_sds_controller_get_base_state_machine(this_controller),
      SCI_BASE_CONTROLLER_STATE_STOPPING
   );

   return SCI_SUCCESS;
}

/**
 * This method is called when the SCIC_SDS_CONTROLLER is in the ready state
 * and the start io handler is called.
 *    - Start the io request on the remote device
 *    - if successful
 *       - assign the io_request to the io_request_table
 *       - post the request to the hardware
 *
 * @param[in] controller This is SCI_BASE_CONTROLLER object which is cast
 *       into a SCIC_SDS_CONTROLLER object.
 * @param[in] remote_device This is SCI_BASE_REMOTE_DEVICE which is cast to a
 *       SCIC_SDS_REMOTE_DEVICE object.
 * @param[in] io_request This is the SCI_BASE_REQUEST which is cast to a
 *       SCIC_SDS_IO_REQUEST object.
 * @param[in] io_tag This is the IO tag to be assigned to the IO request or
 *       SCI_CONTROLLER_INVALID_IO_TAG.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS if the start io operation succeeds
 * @retval SCI_FAILURE_INSUFFICIENT_RESOURCES if the IO tag could not be
 *         allocated for the io request.
 * @retval SCI_FAILURE_INVALID_STATE if one or more objects are not in a valid
 *         state to accept io requests.
 *
 * @todo How does the io_tag parameter get assigned to the io request?
 */
static
SCI_STATUS scic_sds_controller_ready_state_start_io_handler(
   SCI_BASE_CONTROLLER_T    *controller,
   SCI_BASE_REMOTE_DEVICE_T *remote_device,
   SCI_BASE_REQUEST_T       *io_request,
   U16                       io_tag
)
{
   SCI_STATUS status;

   SCIC_SDS_CONTROLLER_T    *this_controller;
   SCIC_SDS_REQUEST_T       *the_request;
   SCIC_SDS_REMOTE_DEVICE_T *the_device;

   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;
   the_request = (SCIC_SDS_REQUEST_T *)io_request;
   the_device = (SCIC_SDS_REMOTE_DEVICE_T *)remote_device;

   status = scic_sds_remote_device_start_io(this_controller, the_device, the_request);

   if (status == SCI_SUCCESS)
   {
      this_controller->io_request_table[
            scic_sds_io_tag_get_index(the_request->io_tag)] = the_request;

      scic_sds_controller_post_request(
         this_controller,
         scic_sds_request_get_post_context(the_request)
      );
   }

   return status;
}

/**
 * This method is called when the SCIC_SDS_CONTROLLER is in the ready state
 * and the complete io handler is called.
 *    - Complete the io request on the remote device
 *    - if successful
 *       - remove the io_request to the io_request_table
 *
 * @param[in] controller This is SCI_BASE_CONTROLLER object which is cast
 *       into a SCIC_SDS_CONTROLLER object.
 * @param[in] remote_device This is SCI_BASE_REMOTE_DEVICE which is cast to a
 *       SCIC_SDS_REMOTE_DEVICE object.
 * @param[in] io_request This is the SCI_BASE_REQUEST which is cast to a
 *       SCIC_SDS_IO_REQUEST object.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS if the start io operation succeeds
 * @retval SCI_FAILURE_INVALID_STATE if one or more objects are not in a valid
 *         state to accept io requests.
 */
static
SCI_STATUS scic_sds_controller_ready_state_complete_io_handler(
   SCI_BASE_CONTROLLER_T    *controller,
   SCI_BASE_REMOTE_DEVICE_T *remote_device,
   SCI_BASE_REQUEST_T       *io_request
)
{
   U16        index;
   SCI_STATUS status;
   SCIC_SDS_CONTROLLER_T    *this_controller;
   SCIC_SDS_REQUEST_T       *the_request;
   SCIC_SDS_REMOTE_DEVICE_T *the_device;

   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;
   the_request = (SCIC_SDS_REQUEST_T *)io_request;
   the_device = (SCIC_SDS_REMOTE_DEVICE_T *)remote_device;

   status = scic_sds_remote_device_complete_io(
                  this_controller, the_device, the_request);

   if (status == SCI_SUCCESS)
   {
      index = scic_sds_io_tag_get_index(the_request->io_tag);
      this_controller->io_request_table[index] = SCI_INVALID_HANDLE;
   }

   return status;
}

/**
 * This method is called when the SCIC_SDS_CONTROLLER is in the ready state
 * and the continue io handler is called.
 *
 * @param[in] controller This is SCI_BASE_CONTROLLER object which is cast
 *       into a SCIC_SDS_CONTROLLER object.
 * @param[in] remote_device This is SCI_BASE_REMOTE_DEVICE which is cast to a
 *       SCIC_SDS_REMOTE_DEVICE object.
 * @param[in] io_request This is the SCI_BASE_REQUEST which is cast to a
 *       SCIC_SDS_IO_REQUEST object.
 *
 * @return SCI_STATUS
 */
static
SCI_STATUS scic_sds_controller_ready_state_continue_io_handler(
   SCI_BASE_CONTROLLER_T    *controller,
   SCI_BASE_REMOTE_DEVICE_T *remote_device,
   SCI_BASE_REQUEST_T       *io_request
)
{
   SCIC_SDS_CONTROLLER_T *this_controller;
   SCIC_SDS_REQUEST_T    *the_request;

   the_request     = (SCIC_SDS_REQUEST_T *)io_request;
   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   this_controller->io_request_table[
      scic_sds_io_tag_get_index(the_request->io_tag)] = the_request;

   scic_sds_controller_post_request(
      this_controller,
      scic_sds_request_get_post_context(the_request)
   );

   return SCI_SUCCESS;
}

/**
 * This method is called when the SCIC_SDS_CONTROLLER is in the ready state
 * and the start task handler is called.
 *    - The remote device is requested to start the task request
 *    - if successful
 *       - assign the task to the io_request_table
 *       - post the request to the SCU hardware
 *
 * @param[in] controller This is SCI_BASE_CONTROLLER object which is cast
 *       into a SCIC_SDS_CONTROLLER object.
 * @param[in] remote_device This is SCI_BASE_REMOTE_DEVICE which is cast to a
 *       SCIC_SDS_REMOTE_DEVICE object.
 * @param[in] io_request This is the SCI_BASE_REQUEST which is cast to a
 *       SCIC_SDS_IO_REQUEST object.
 * @param[in] task_tag This is the task tag to be assigned to the task request
 *       or SCI_CONTROLLER_INVALID_IO_TAG.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS if the start io operation succeeds
 * @retval SCI_FAILURE_INSUFFICIENT_RESOURCES if the IO tag could not be
 *         allocated for the io request.
 * @retval SCI_FAILURE_INVALID_STATE if one or more objects are not in a valid
 *         state to accept io requests.
 *
 * @todo How does the io tag get assigned in this code path?
 */
static
SCI_STATUS scic_sds_controller_ready_state_start_task_handler(
   SCI_BASE_CONTROLLER_T    *controller,
   SCI_BASE_REMOTE_DEVICE_T *remote_device,
   SCI_BASE_REQUEST_T       *io_request,
   U16                       task_tag
)
{
   SCIC_SDS_CONTROLLER_T    *this_controller = (SCIC_SDS_CONTROLLER_T *)
                                               controller;
   SCIC_SDS_REQUEST_T       *the_request     = (SCIC_SDS_REQUEST_T *)
                                               io_request;
   SCIC_SDS_REMOTE_DEVICE_T *the_device      = (SCIC_SDS_REMOTE_DEVICE_T *)
                                               remote_device;
   SCI_STATUS                status;

   status = scic_sds_remote_device_start_task(
               this_controller, the_device, the_request
            );

   if (status == SCI_SUCCESS)
   {
      this_controller->io_request_table[
         scic_sds_io_tag_get_index(the_request->io_tag)] = the_request;

      scic_sds_controller_post_request(
         this_controller,
         scic_sds_request_get_post_context(the_request)
      );
   }
   else if (status == SCI_FAILURE_RESET_DEVICE_PARTIAL_SUCCESS)
   {
      this_controller->io_request_table[
         scic_sds_io_tag_get_index(the_request->io_tag)] = the_request;

      //We will let framework know this task request started successfully,
      //although core is still woring on starting the request (to post tc when
      //RNC is resumed.)
      status = SCI_SUCCESS;
   }
   return status;
}

/**
 * This method is called when the SCIC_SDS_CONTROLLER is in the ready state
 * and the terminate request handler is called.
 *    - call the io request terminate function
 *    - if successful
 *       - post the terminate request to the SCU hardware
 *
 * @param[in] controller This is SCI_BASE_CONTROLLER object which is cast
 *       into a SCIC_SDS_CONTROLLER object.
 * @param[in] remote_device This is SCI_BASE_REMOTE_DEVICE which is cast to a
 *       SCIC_SDS_REMOTE_DEVICE object.
 * @param[in] io_request This is the SCI_BASE_REQUEST which is cast to a
 *       SCIC_SDS_IO_REQUEST object.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS if the start io operation succeeds
 * @retval SCI_FAILURE_INVALID_STATE if one or more objects are not in a valid
 *         state to accept io requests.
 */
static
SCI_STATUS scic_sds_controller_ready_state_terminate_request_handler(
   SCI_BASE_CONTROLLER_T    *controller,
   SCI_BASE_REMOTE_DEVICE_T *remote_device,
   SCI_BASE_REQUEST_T       *io_request
)
{
   SCIC_SDS_CONTROLLER_T    *this_controller = (SCIC_SDS_CONTROLLER_T *)
                                               controller;
   SCIC_SDS_REQUEST_T       *the_request     = (SCIC_SDS_REQUEST_T *)
                                               io_request;
   SCI_STATUS                status;

   status = scic_sds_io_request_terminate(the_request);
   if (status == SCI_SUCCESS)
   {
      // Utilize the original post context command and or in the POST_TC_ABORT
      // request sub-type.
      scic_sds_controller_post_request(
         this_controller,
         scic_sds_request_get_post_context(the_request)
         | SCU_CONTEXT_COMMAND_REQUEST_POST_TC_ABORT
      );
   }

   return status;
}

/**
 * This method is called when the SCIC_SDS_CONTROLLER is in the starting state
 * link up handler is called.  This method will perform the following:
 *    - Stop the phy timer
 *    - Start the next phy
 *    - Report the link up condition to the port object
 *
 * @param[in] controller This is SCIC_SDS_CONTROLLER which receives the link up
 *       notification.
 * @param[in] port This is SCIC_SDS_PORT with which the phy is associated.
 * @param[in] phy This is the SCIC_SDS_PHY which has gone link up.
 *
 * @return none
 */
static
void scic_sds_controller_ready_state_link_up_handler(
   SCIC_SDS_CONTROLLER_T *this_controller,
   SCIC_SDS_PORT_T       *port,
   SCIC_SDS_PHY_T        *phy
)
{
   this_controller->port_agent.link_up_handler(
      this_controller, &this_controller->port_agent, port, phy
   );
}

/**
 * This method is called when the SCIC_SDS_CONTROLLER is in the starting state
 * link down handler is called.
 *    - Report the link down condition to the port object
 *
 * @param[in] controller This is SCIC_SDS_CONTROLLER which receives the
 *       link down notification.
 * @param[in] port This is SCIC_SDS_PORT with which the phy is associated.
 * @param[in] phy This is the SCIC_SDS_PHY which has gone link down.
 *
 * @return none
 */
static
void scic_sds_controller_ready_state_link_down_handler(
   SCIC_SDS_CONTROLLER_T *this_controller,
   SCIC_SDS_PORT_T       *port,
   SCIC_SDS_PHY_T        *phy
)
{
   this_controller->port_agent.link_down_handler(
      this_controller, &this_controller->port_agent, port, phy
   );
}

//*****************************************************************************
//* STOPPING STATE HANDLERS
//*****************************************************************************

/**
 * This method is called when the SCIC_SDS_CONTROLLER is in a stopping state
 * and the complete io handler is called.
 *    - This function is not yet implemented
 *
 * @param[in] controller This is SCI_BASE_CONTROLLER object which is cast
 *       into a SCIC_SDS_CONTROLLER object.
 * @param[in] remote_device This is SCI_BASE_REMOTE_DEVICE which is cast to a
 *       SCIC_SDS_REMOTE_DEVICE object.
 * @param[in] io_request This is the SCI_BASE_REQUEST which is cast to a
 *       SCIC_SDS_IO_REQUEST object.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE
 */
static
SCI_STATUS scic_sds_controller_stopping_state_complete_io_handler(
   SCI_BASE_CONTROLLER_T    *controller,
   SCI_BASE_REMOTE_DEVICE_T *remote_device,
   SCI_BASE_REQUEST_T       *io_request
)
{
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   /// @todo Implement this function
   return SCI_FAILURE;
}

/**
 * This method is called when the SCIC_SDS_CONTROLLER is in a stopping state
 * and the a remote device has stopped.
 *
 * @param[in] controller This is SCI_BASE_CONTROLLER object which is cast
 *       into a SCIC_SDS_CONTROLLER object.
 * @param[in] remote_device This is SCI_BASE_REMOTE_DEVICE which is cast to a
 *       SCIC_SDS_REMOTE_DEVICE object.
 *
 * @return none
 */
static
void scic_sds_controller_stopping_state_device_stopped_handler(
   SCIC_SDS_CONTROLLER_T    * controller,
   SCIC_SDS_REMOTE_DEVICE_T * remote_device
)
{
   if (!scic_sds_controller_has_remote_devices_stopping(controller))
   {
      sci_base_state_machine_change_state(
         &controller->parent.state_machine,
         SCI_BASE_CONTROLLER_STATE_STOPPED
      );
   }
}

//*****************************************************************************
//* STOPPED STATE HANDLERS
//*****************************************************************************

//*****************************************************************************
//* FAILED STATE HANDLERS
//*****************************************************************************

/**
 * This method is called when the SCIC_SDS_CONTROLLER failed state start
 * io/task handler is in place.
 *    - Issue a warning message
 *
 * @param[in] controller This is SCI_BASE_CONTROLLER object which is cast
 *       into a SCIC_SDS_CONTROLLER object.
 * @param[in] remote_device This is SCI_BASE_REMOTE_DEVICE which, if it was
 *       used, would be cast to a SCIC_SDS_REMOTE_DEVICE.
 * @param[in] io_request This is the SCI_BASE_REQUEST which, if it was used,
 *       would be cast to a SCIC_SDS_IO_REQUEST.
 * @param[in] io_tag This is the IO tag to be assigned to the IO request or
 *       SCI_CONTROLLER_INVALID_IO_TAG.
 *
 * @return SCI_FAILURE
 * @retval SCI_FAILURE
 */
static
SCI_STATUS scic_sds_controller_failed_state_start_operation_handler(
   SCI_BASE_CONTROLLER_T    *controller,
   SCI_BASE_REMOTE_DEVICE_T *remote_device,
   SCI_BASE_REQUEST_T       *io_request,
   U16                       io_tag
)
{
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

   SCIC_LOG_WARNING((
      sci_base_object_get_logger(this_controller),
      SCIC_LOG_OBJECT_CONTROLLER,
      "SCIC Controller requested to start an io/task from failed state %d\n",
      sci_base_state_machine_get_state(
         scic_sds_controller_get_base_state_machine(this_controller))
   ));

   return SCI_FAILURE;
}

/**
 * This method is called when the SCIC_SDS_CONTROLLER is in the failed state
 * reset handler is in place.
 *    - Transition to SCI_BASE_CONTROLLER_STATE_RESETTING
 *
 * @param[in] controller The SCI_BASE_CONTROLLER object which is cast into a
 *       SCIC_SDS_CONTROLLER object.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE if fatal memory error occurred
 */
static
SCI_STATUS scic_sds_controller_failed_state_reset_handler(
   SCI_BASE_CONTROLLER_T *controller
)
{
    SCIC_SDS_CONTROLLER_T *this_controller;
    this_controller = (SCIC_SDS_CONTROLLER_T *)controller;

    if (this_controller->parent.error == SCI_CONTROLLER_FATAL_MEMORY_ERROR) {
        SCIC_LOG_TRACE((
           sci_base_object_get_logger(controller),
           SCIC_LOG_OBJECT_CONTROLLER,
           "scic_sds_controller_resetting_state_enter(0x%x) enter\n not allowed with fatal memory error",
           controller
        ));

        return SCI_FAILURE;
    } else {
        return scic_sds_controller_general_reset_handler(controller);
    }
}

/**
 * This method is called when the SCIC_SDS_CONTROLLER is in the failed state
 * and the terminate request handler is called.
 *    - call the io request terminate function
 *    - if successful
 *       - post the terminate request to the SCU hardware
 *
 * @param[in] controller This is SCI_BASE_CONTROLLER object which is cast
 *       into a SCIC_SDS_CONTROLLER object.
 * @param[in] remote_device This is SCI_BASE_REMOTE_DEVICE which is cast to a
 *       SCIC_SDS_REMOTE_DEVICE object.
 * @param[in] io_request This is the SCI_BASE_REQUEST which is cast to a
 *       SCIC_SDS_IO_REQUEST object.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS if the start io operation succeeds
 * @retval SCI_FAILURE_INVALID_STATE if one or more objects are not in a valid
 *         state to accept io requests.
 */
static
SCI_STATUS scic_sds_controller_failed_state_terminate_request_handler(
   SCI_BASE_CONTROLLER_T    *controller,
   SCI_BASE_REMOTE_DEVICE_T *remote_device,
   SCI_BASE_REQUEST_T       *io_request
)
{
   SCIC_SDS_REQUEST_T       *the_request     = (SCIC_SDS_REQUEST_T *)
                                               io_request;

   return scic_sds_io_request_terminate(the_request);
}

SCIC_SDS_CONTROLLER_STATE_HANDLER_T
   scic_sds_controller_state_handler_table[SCI_BASE_CONTROLLER_MAX_STATES] =
{
   // SCI_BASE_CONTROLLER_STATE_INITIAL
   {
      {
         NULL,
         NULL,
         NULL,
         NULL,
         scic_sds_controller_default_start_operation_handler,
         scic_sds_controller_default_start_operation_handler,
         scic_sds_controller_default_request_handler,
         scic_sds_controller_default_request_handler,
         scic_sds_controller_default_request_handler,
         NULL,
         NULL
      },
      scic_sds_controller_default_request_handler,
      NULL,
      NULL,
      NULL,
      NULL
   },
   // SCI_BASE_CONTROLLER_STATE_RESET
   {
      {
         NULL,
         NULL,
         NULL,
         scic_sds_controller_reset_state_initialize_handler,
         scic_sds_controller_default_start_operation_handler,
         scic_sds_controller_default_start_operation_handler,
         scic_sds_controller_default_request_handler,
         scic_sds_controller_default_request_handler,
         scic_sds_controller_default_request_handler,
         NULL,
         NULL
      },
      scic_sds_controller_default_request_handler,
      NULL,
      NULL,
      NULL,
      NULL
   },
   // SCI_BASE_CONTROLLER_STATE_INITIALIZING
   {
      {
         NULL,
         NULL,
         NULL,
         NULL,
         scic_sds_controller_default_start_operation_handler,
         scic_sds_controller_default_start_operation_handler,
         scic_sds_controller_default_request_handler,
         scic_sds_controller_default_request_handler,
         scic_sds_controller_default_request_handler,
         NULL,
         NULL
      },
      scic_sds_controller_default_request_handler,
      NULL,
      NULL,
      NULL,
      NULL
   },
   // SCI_BASE_CONTROLLER_STATE_INITIALIZED
   {
      {
         scic_sds_controller_initialized_state_start_handler,
         NULL,
         NULL,
         NULL,
         scic_sds_controller_default_start_operation_handler,
         scic_sds_controller_default_start_operation_handler,
         scic_sds_controller_default_request_handler,
         scic_sds_controller_default_request_handler,
         scic_sds_controller_default_request_handler,
         NULL,
         NULL
      },
      scic_sds_controller_default_request_handler,
      NULL,
      NULL,
      NULL,
      NULL
   },
   // SCI_BASE_CONTROLLER_STATE_STARTING
   {
      {
         NULL,
         NULL,
         NULL,
         NULL,
         scic_sds_controller_default_start_operation_handler,
         scic_sds_controller_default_start_operation_handler,
         scic_sds_controller_default_request_handler,
         scic_sds_controller_default_request_handler,
         scic_sds_controller_default_request_handler,
         NULL,
         NULL
      },
      scic_sds_controller_default_request_handler,
      scic_sds_controller_starting_state_link_up_handler,
      scic_sds_controller_starting_state_link_down_handler,
      NULL,
      NULL
   },
   // SCI_BASE_CONTROLLER_STATE_READY
   {
      {
         NULL,
         scic_sds_controller_ready_state_stop_handler,
         scic_sds_controller_general_reset_handler,
         NULL,
         scic_sds_controller_ready_state_start_io_handler,
         scic_sds_controller_default_start_operation_handler,
         scic_sds_controller_ready_state_complete_io_handler,
         scic_sds_controller_default_request_handler,
         scic_sds_controller_ready_state_continue_io_handler,
         scic_sds_controller_ready_state_start_task_handler,
         scic_sds_controller_ready_state_complete_io_handler
      },
      scic_sds_controller_ready_state_terminate_request_handler,
      scic_sds_controller_ready_state_link_up_handler,
      scic_sds_controller_ready_state_link_down_handler,
      NULL,
      NULL
   },
   // SCI_BASE_CONTROLLER_STATE_RESETTING
   {
      {
         NULL,
         NULL,
         NULL,
         NULL,
         scic_sds_controller_default_start_operation_handler,
         scic_sds_controller_default_start_operation_handler,
         scic_sds_controller_default_request_handler,
         scic_sds_controller_default_request_handler,
         scic_sds_controller_default_request_handler,
         NULL,
         NULL
      },
      scic_sds_controller_default_request_handler,
      NULL,
      NULL,
      NULL,
      NULL
   },
   // SCI_BASE_CONTROLLER_STATE_STOPPING
   {
      {
         NULL,
         NULL,
         NULL,
         NULL,
         scic_sds_controller_default_start_operation_handler,
         scic_sds_controller_default_start_operation_handler,
         scic_sds_controller_stopping_state_complete_io_handler,
         scic_sds_controller_default_request_handler,
         scic_sds_controller_default_request_handler,
         NULL,
         NULL
      },
      scic_sds_controller_default_request_handler,
      NULL,
      NULL,
      NULL,
      scic_sds_controller_stopping_state_device_stopped_handler
   },
   // SCI_BASE_CONTROLLER_STATE_STOPPED
   {
      {
         NULL,
         NULL,
         scic_sds_controller_failed_state_reset_handler,
         NULL,
         scic_sds_controller_default_start_operation_handler,
         scic_sds_controller_default_start_operation_handler,
         scic_sds_controller_default_request_handler,
         scic_sds_controller_default_request_handler,
         scic_sds_controller_default_request_handler,
         NULL,
         NULL
      },
      scic_sds_controller_default_request_handler,
      NULL,
      NULL,
      NULL,
      NULL
   },
   // SCI_BASE_CONTROLLER_STATE_FAILED
   {
      {
         NULL,
         NULL,
         scic_sds_controller_general_reset_handler,
         NULL,
         scic_sds_controller_failed_state_start_operation_handler,
         scic_sds_controller_failed_state_start_operation_handler,
         scic_sds_controller_default_request_handler,
         scic_sds_controller_default_request_handler,
         scic_sds_controller_default_request_handler,
         NULL,
         NULL
      },
      scic_sds_controller_failed_state_terminate_request_handler,
      NULL,
      NULL,
      NULL
   }
};

/**
 * This method implements the actions taken by the SCIC_SDS_CONTROLLER on
 * entry to the SCI_BASE_CONTROLLER_STATE_INITIAL.
 *    - Set the state handlers to the controllers initial state.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_CONTROLLER object.
 *
 * @return none
 *
 * @todo This function should initialze the controller object.
 */
static
void scic_sds_controller_initial_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller= (SCIC_SDS_CONTROLLER_T *)object;

   scic_sds_controller_set_base_state_handlers(
      this_controller, SCI_BASE_CONTROLLER_STATE_INITIAL);

   sci_base_state_machine_change_state(
      &this_controller->parent.state_machine, SCI_BASE_CONTROLLER_STATE_RESET);
}

/**
 * This method implements the actions taken by the SCIC_SDS_CONTROLLER on
 * entry to the SCI_BASE_CONTROLLER_STATE_RESET.
 *    - Set the state handlers to the controllers reset state.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_CONTROLLER object.
 *
 * @return none
 */
static
void scic_sds_controller_reset_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   U8 index;
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller= (SCIC_SDS_CONTROLLER_T *)object;

   scic_sds_controller_set_base_state_handlers(
      this_controller, SCI_BASE_CONTROLLER_STATE_RESET);

   scic_sds_port_configuration_agent_construct(&this_controller->port_agent);

   // Construct the ports for this controller
   for (index = 0; index < (SCI_MAX_PORTS + 1); index++)
   {
      scic_sds_port_construct(
         &this_controller->port_table[index],
         (index == SCI_MAX_PORTS) ? SCIC_SDS_DUMMY_PORT : index,
         this_controller
      );
   }

   // Construct the phys for this controller
   for (index = 0; index < SCI_MAX_PHYS; index++)
   {
      // Add all the PHYs to the dummy port
      scic_sds_phy_construct(
         &this_controller->phy_table[index],
         &this_controller->port_table[SCI_MAX_PORTS],
         index
      );
   }

   this_controller->invalid_phy_mask = 0;

   // Set the default maximum values
   this_controller->completion_event_entries      = SCU_EVENT_COUNT;
   this_controller->completion_queue_entries      = SCU_COMPLETION_QUEUE_COUNT;
   this_controller->remote_node_entries           = SCI_MAX_REMOTE_DEVICES;
   this_controller->logical_port_entries          = SCI_MAX_PORTS;
   this_controller->task_context_entries          = SCU_IO_REQUEST_COUNT;
   this_controller->uf_control.buffers.count      = SCU_UNSOLICITED_FRAME_COUNT;
   this_controller->uf_control.address_table.count= SCU_UNSOLICITED_FRAME_COUNT;

   // Initialize the User and OEM parameters to default values.
   scic_sds_controller_set_default_config_parameters(this_controller);
}

/**
 * This method implements the actions taken by the SCIC_SDS_CONTROLLER on
 * entry to the SCI_BASE_CONTROLLER_STATE_INITIALIZING.
 *    - Set the state handlers to the controllers initializing state.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_CONTROLLER object.
 *
 * @return none
 */
static
void scic_sds_controller_initializing_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller= (SCIC_SDS_CONTROLLER_T *)object;

   scic_sds_controller_set_base_state_handlers(
      this_controller, SCI_BASE_CONTROLLER_STATE_INITIALIZING);
}

/**
 * This method implements the actions taken by the SCIC_SDS_CONTROLLER on
 * entry to the SCI_BASE_CONTROLLER_STATE_INITIALIZED.
 *    - Set the state handlers to the controllers initialized state.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_CONTROLLER object.
 *
 * @return none
 */
static
void scic_sds_controller_initialized_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller= (SCIC_SDS_CONTROLLER_T *)object;

   scic_sds_controller_set_base_state_handlers(
      this_controller, SCI_BASE_CONTROLLER_STATE_INITIALIZED);
}

/**
 * This method implements the actions taken by the SCIC_SDS_CONTROLLER on
 * entry to the SCI_BASE_CONTROLLER_STATE_STARTING.
 *    - Set the state handlers to the controllers starting state.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_CONTROLLER object.
 *
 * @return none
 */
static
void scic_sds_controller_starting_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller= (SCIC_SDS_CONTROLLER_T *)object;

   scic_sds_controller_set_base_state_handlers(
      this_controller, SCI_BASE_CONTROLLER_STATE_STARTING);

}

/**
 * This method implements the actions taken by the SCIC_SDS_CONTROLLER on exit
 * from the SCI_BASE_CONTROLLER_STATE_STARTING.
 *    - This function stops the controller starting timeout timer.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_CONTROLLER object.
 *
 * @return none
 */
static
void scic_sds_controller_starting_state_exit(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller= (SCIC_SDS_CONTROLLER_T *)object;

   scic_cb_timer_stop(object, this_controller->timeout_timer);

   // We are done with this timer since we are exiting the starting
   // state so remove it
   scic_cb_timer_destroy(
      this_controller,
      this_controller->phy_startup_timer
   );

   this_controller->phy_startup_timer = NULL;
}

/**
 * This method implements the actions taken by the SCIC_SDS_CONTROLLER on
 * entry to the SCI_BASE_CONTROLLER_STATE_READY.
 *    - Set the state handlers to the controllers ready state.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_CONTROLLER object.
 *
 * @return none
 */
static
void scic_sds_controller_ready_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   U32 clock_gating_unit_value;
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller= (SCIC_SDS_CONTROLLER_T *)object;

   scic_sds_controller_set_base_state_handlers(
      this_controller, SCI_BASE_CONTROLLER_STATE_READY);

   /**
    * enable clock gating for power control of the scu unit
    */
   clock_gating_unit_value = SMU_CGUCR_READ(this_controller);

   clock_gating_unit_value &= ~( SMU_CGUCR_GEN_BIT(REGCLK_ENABLE)
                               | SMU_CGUCR_GEN_BIT(TXCLK_ENABLE)
                               | SMU_CGUCR_GEN_BIT(XCLK_ENABLE) );
   clock_gating_unit_value |= SMU_CGUCR_GEN_BIT(IDLE_ENABLE);

   SMU_CGUCR_WRITE(this_controller, clock_gating_unit_value);

   //set the default interrupt coalescence number and timeout value.
   scic_controller_set_interrupt_coalescence(
      this_controller, 0x10, 250);
}

/**
 * This method implements the actions taken by the SCIC_SDS_CONTROLLER on exit
 * from the SCI_BASE_CONTROLLER_STATE_READY.
 *    - This function does nothing.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_CONTROLLER object.
 *
 * @return none
 */
static
void scic_sds_controller_ready_state_exit(
   SCI_BASE_OBJECT_T *object
)
{
   U32 clock_gating_unit_value;
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller= (SCIC_SDS_CONTROLLER_T *)object;

   /**
    * restore clock gating for power control of the scu unit
    */
   clock_gating_unit_value = SMU_CGUCR_READ(this_controller);

   clock_gating_unit_value &= ~SMU_CGUCR_GEN_BIT(IDLE_ENABLE);
   clock_gating_unit_value |= ( SMU_CGUCR_GEN_BIT(REGCLK_ENABLE)
                              | SMU_CGUCR_GEN_BIT(TXCLK_ENABLE)
                              | SMU_CGUCR_GEN_BIT(XCLK_ENABLE) );

   SMU_CGUCR_WRITE(this_controller, clock_gating_unit_value);

   //disable interrupt coalescence.
   scic_controller_set_interrupt_coalescence(this_controller, 0, 0);
}

/**
 * This method implements the actions taken by the SCIC_SDS_CONTROLLER on
 * entry to the SCI_BASE_CONTROLLER_STATE_READY.
 *    - Set the state handlers to the controllers ready state.
 *    - Stop all of the remote devices on this controller
 *    - Stop the ports on this controller
 *    - Stop the phys on this controller
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_CONTROLLER object.
 *
 * @return none
 */
static
void scic_sds_controller_stopping_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller= (SCIC_SDS_CONTROLLER_T *)object;

   scic_sds_controller_set_base_state_handlers(
      this_controller, SCI_BASE_CONTROLLER_STATE_STOPPING);

   // Stop all of the components for this controller in the reverse order
   // from which they are initialized.
   scic_sds_controller_stop_devices(this_controller);
   scic_sds_controller_stop_ports(this_controller);

   if (!scic_sds_controller_has_remote_devices_stopping(this_controller))
   {
      sci_base_state_machine_change_state(
         &this_controller->parent.state_machine,
         SCI_BASE_CONTROLLER_STATE_STOPPED
      );
   }
}

/**
 * This method implements the actions taken by the SCIC_SDS_CONTROLLER on exit
 * from the SCI_BASE_CONTROLLER_STATE_STOPPING.
 *    - This function stops the controller stopping timeout timer.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_CONTROLLER object.
 *
 * @return none
 */
static
void scic_sds_controller_stopping_state_exit(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller= (SCIC_SDS_CONTROLLER_T *)object;

   scic_cb_timer_stop(this_controller, this_controller->timeout_timer);
}

/**
 * This method implements the actions taken by the SCIC_SDS_CONTROLLER on
 * entry to the SCI_BASE_CONTROLLER_STATE_STOPPED.
 *    - Set the state handlers to the controllers stopped state.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_CONTROLLER object.
 *
 * @return none
 */
static
void scic_sds_controller_stopped_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller= (SCIC_SDS_CONTROLLER_T *)object;

   scic_sds_controller_set_base_state_handlers(
      this_controller, SCI_BASE_CONTROLLER_STATE_STOPPED);

   // We are done with this timer until the next timer we initialize
   scic_cb_timer_destroy(
      this_controller,
      this_controller->timeout_timer
   );
   this_controller->timeout_timer = NULL;

   // Controller has stopped so disable all the phys on this controller
   scic_sds_controller_stop_phys(this_controller);

   scic_sds_port_configuration_agent_destroy(
      this_controller,
      &this_controller->port_agent
   );

   scic_cb_controller_stop_complete(this_controller, SCI_SUCCESS);
}

/**
 * This method implements the actions taken by the SCIC_SDS_CONTROLLER on
 * entry to the SCI_BASE_CONTROLLER_STATE_RESETTING.
 *    - Set the state handlers to the controllers resetting state.
 *    - Write to the SCU hardware reset register to force a reset
 *    - Transition to the SCI_BASE_CONTROLLER_STATE_RESET
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_CONTROLLER object.
 *
 * @return none
 */
static
void scic_sds_controller_resetting_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller= (SCIC_SDS_CONTROLLER_T *)object;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_controller),
      SCIC_LOG_OBJECT_CONTROLLER,
      "scic_sds_controller_resetting_state_enter(0x%x) enter\n",
      this_controller
   ));

   scic_sds_controller_set_base_state_handlers(
      this_controller, SCI_BASE_CONTROLLER_STATE_RESETTING);

   scic_sds_controller_reset_hardware(this_controller);

   sci_base_state_machine_change_state(
      scic_sds_controller_get_base_state_machine(this_controller),
      SCI_BASE_CONTROLLER_STATE_RESET
   );
}

static
SCI_STATUS scic_sds_abort_reqests(
        SCIC_SDS_CONTROLLER_T * controller,
        SCIC_SDS_REMOTE_DEVICE_T * remote_device,
        SCIC_SDS_PORT_T * port
)
{
    SCI_STATUS          status           = SCI_SUCCESS;
    SCI_STATUS          terminate_status = SCI_SUCCESS;
    SCIC_SDS_REQUEST_T *the_request;
    U32                 index;
    U32                 request_count;

    if (remote_device != NULL)
        request_count = remote_device->started_request_count;
    else if (port != NULL)
        request_count = port->started_request_count;
    else
        request_count = SCI_MAX_IO_REQUESTS;


    for (index = 0;
         (index < SCI_MAX_IO_REQUESTS) && (request_count > 0);
         index++)
    {
       the_request = controller->io_request_table[index];

       if (the_request != NULL)
       {
           if (the_request->target_device == remote_device
                   || the_request->target_device->owning_port == port
                   || (remote_device == NULL && port == NULL))
           {
               terminate_status = scic_controller_terminate_request(
                                     controller,
                                     the_request->target_device,
                                     the_request
                                  );

               if (terminate_status != SCI_SUCCESS)
                  status = terminate_status;

               request_count--;
           }
       }
    }

    return status;
}

SCI_STATUS scic_sds_terminate_reqests(
        SCIC_SDS_CONTROLLER_T *this_controller,
        SCIC_SDS_REMOTE_DEVICE_T *this_remote_device,
        SCIC_SDS_PORT_T *this_port
)
{
    SCI_STATUS status = SCI_SUCCESS;
    SCI_STATUS abort_status = SCI_SUCCESS;

    // move all request to abort state
    abort_status = scic_sds_abort_reqests(this_controller, this_remote_device, this_port);

    if (abort_status != SCI_SUCCESS)
        status = abort_status;

    //move all request to complete state
    if (this_controller->parent.error == SCI_CONTROLLER_FATAL_MEMORY_ERROR)
        abort_status = scic_sds_abort_reqests(this_controller, this_remote_device, this_port);

    if (abort_status != SCI_SUCCESS)
        status = abort_status;

    return status;
}

static
SCI_STATUS scic_sds_terminate_all_requests(
        SCIC_SDS_CONTROLLER_T * controller
)
{
    return scic_sds_terminate_reqests(controller, NULL, NULL);
}

/**
 * This method implements the actions taken by the SCIC_SDS_CONTROLLER on
 * entry to the SCI_BASE_CONTROLLER_STATE_FAILED.
 *    - Set the state handlers to the controllers failed state.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast to a
 *       SCIC_SDS_CONTROLLER object.
 *
 * @return none
 */
static
void scic_sds_controller_failed_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_CONTROLLER_T *this_controller;
   this_controller= (SCIC_SDS_CONTROLLER_T *)object;

   scic_sds_controller_set_base_state_handlers(
      this_controller, SCI_BASE_CONTROLLER_STATE_FAILED);

   if (this_controller->parent.error == SCI_CONTROLLER_FATAL_MEMORY_ERROR)
   scic_sds_terminate_all_requests(this_controller);
   else
       scic_sds_controller_release_resource(this_controller);

   //notify framework the controller failed.
   scic_cb_controller_error(this_controller,
           this_controller->parent.error);
}

// ---------------------------------------------------------------------------

SCI_BASE_STATE_T
   scic_sds_controller_state_table[SCI_BASE_CONTROLLER_MAX_STATES] =
{
   {
      SCI_BASE_CONTROLLER_STATE_INITIAL,
      scic_sds_controller_initial_state_enter,
      NULL,
   },
   {
      SCI_BASE_CONTROLLER_STATE_RESET,
      scic_sds_controller_reset_state_enter,
      NULL,
   },
   {
      SCI_BASE_CONTROLLER_STATE_INITIALIZING,
      scic_sds_controller_initializing_state_enter,
      NULL,
   },
   {
      SCI_BASE_CONTROLLER_STATE_INITIALIZED,
      scic_sds_controller_initialized_state_enter,
      NULL,
   },
   {
      SCI_BASE_CONTROLLER_STATE_STARTING,
      scic_sds_controller_starting_state_enter,
      scic_sds_controller_starting_state_exit,
   },
   {
      SCI_BASE_CONTROLLER_STATE_READY,
      scic_sds_controller_ready_state_enter,
      scic_sds_controller_ready_state_exit,
   },
   {
      SCI_BASE_CONTROLLER_STATE_RESETTING,
      scic_sds_controller_resetting_state_enter,
      NULL,
   },
   {
      SCI_BASE_CONTROLLER_STATE_STOPPING,
      scic_sds_controller_stopping_state_enter,
      scic_sds_controller_stopping_state_exit,
   },
   {
      SCI_BASE_CONTROLLER_STATE_STOPPED,
      scic_sds_controller_stopped_state_enter,
      NULL,
   },
   {
      SCI_BASE_CONTROLLER_STATE_FAILED,
      scic_sds_controller_failed_state_enter,
      NULL,
   }
};

