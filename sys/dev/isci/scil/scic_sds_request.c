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
 * @brief This file contains the implementation for the operations on an
 *        SCIC_SDS_IO_REQUEST object.
 */

#include <dev/isci/scil/intel_sat.h>
#include <dev/isci/scil/intel_sata.h>
#include <dev/isci/scil/intel_sas.h>
#include <dev/isci/scil/sci_util.h>
#include <dev/isci/scil/sci_base_request.h>
#include <dev/isci/scil/scic_controller.h>
#include <dev/isci/scil/scic_io_request.h>
#include <dev/isci/scil/scic_remote_device.h>
#include <dev/isci/scil/scic_user_callback.h>
#include <dev/isci/scil/scic_sds_logger.h>
#include <dev/isci/scil/scic_sds_request.h>
#include <dev/isci/scil/scic_sds_pci.h>
#include <dev/isci/scil/scic_sds_stp_request.h>
#include <dev/isci/scil/scic_sds_controller.h>
#include <dev/isci/scil/scic_sds_controller_registers.h>
#include <dev/isci/scil/scic_sds_remote_device.h>
#include <dev/isci/scil/scic_sds_port.h>
#include <dev/isci/scil/scic_task_request.h>
#include <dev/isci/scil/scu_constants.h>
#include <dev/isci/scil/scu_task_context.h>
#include <dev/isci/scil/scic_sds_smp_request.h>
#include <dev/isci/sci_environment.h>
#include <dev/isci/scil/scic_sds_unsolicited_frame_control.h>
#include <dev/isci/scil/sci_types.h>
#include <dev/isci/scil/scu_completion_codes.h>
#include <dev/isci/scil/intel_scsi.h>

#if !defined(DISABLE_ATAPI)
#include <dev/isci/scil/scic_sds_stp_packet_request.h>
#endif

/**
* @struct SCI_SINGLE_LEVEL_LUN
*
* @brief this struct describes the single level LUN structure
*        as per the SAM 4.
*/
typedef struct SCI_SINGLE_LEVEL_LUN
{
    U8  bus_id              : 6;
    U8  address_method      : 2;
    U8  lun_number;
    U8  second_level_lun[2];
    U8  third_level_lun[2];
    U8  forth_level_lun[2];

} SCI_SINGLE_LEVEL_LUN_T;


//****************************************************************************
//* SCIC SDS IO REQUEST CONSTANTS
//****************************************************************************

/**
 * We have no timer requirements for IO requests right now
 */
#define SCIC_SDS_IO_REQUEST_MINIMUM_TIMER_COUNT (0)
#define SCIC_SDS_IO_REQUEST_MAXIMUM_TIMER_COUNT (0)

//****************************************************************************
//* SCIC SDS IO REQUEST MACROS
//****************************************************************************

/**
 * This is a helper macro to return the os handle for this request object.
 */
#define scic_sds_request_get_user_request(request) \
   ((request)->user_request)


/**
 * This macro returns the sizeof memory required to store the an SSP IO
 * request.  This does not include the size of the SGL or SCU Task Context
 * memory.The sizeof(U32) are needed for DWORD alignment of the command IU
 * and response IU
 */
#define scic_ssp_io_request_get_object_size() \
   ( \
       sizeof(SCI_SSP_COMMAND_IU_T) \
     + sizeof (U32) \
     + sizeof(SCI_SSP_RESPONSE_IU_T) \
     + sizeof (U32) \
   )

/**
 * This macro returns the address of the ssp command buffer in the io
 * request memory
 */
#define scic_sds_ssp_request_get_command_buffer_unaligned(memory) \
   ((SCI_SSP_COMMAND_IU_T *)( \
      ((char *)(memory)) + sizeof(SCIC_SDS_REQUEST_T) \
   ))

/**
 * This macro aligns the ssp command buffer in DWORD alignment
*/
#define scic_sds_ssp_request_align_command_buffer(address) \
   ((SCI_SSP_COMMAND_IU_T *)( \
      (((POINTER_UINT)(address)) + (sizeof(U32) - 1)) \
         & ~(sizeof(U32)- 1) \
   ))

/**
 * This macro returns the DWORD-aligned ssp command buffer
*/
#define scic_sds_ssp_request_get_command_buffer(memory) \
   ((SCI_SSP_COMMAND_IU_T *)  \
      ((char *)scic_sds_ssp_request_align_command_buffer( \
         (char *) scic_sds_ssp_request_get_command_buffer_unaligned(memory) \
   )))

/**
 * This macro returns the address of the ssp response buffer in the io
 * request memory
 */
#define scic_sds_ssp_request_get_response_buffer_unaligned(memory) \
   ((SCI_SSP_RESPONSE_IU_T *)( \
         ((char *)(scic_sds_ssp_request_get_command_buffer(memory))) \
       + sizeof(SCI_SSP_COMMAND_IU_T) \
   ))

/**
 * This macro aligns the ssp response buffer in DWORD-aligned fashion
 */
#define scic_sds_ssp_request_align_response_buffer(memory) \
   ((SCI_SSP_RESPONSE_IU_T *)( \
      (((POINTER_UINT)(memory)) + (sizeof(U32) - 1)) \
         & ~(sizeof(U32)- 1) \
   ))

/**
 * This macro returns the DWORD-aligned ssp response buffer
*/
#define scic_sds_ssp_request_get_response_buffer(memory) \
   ((SCI_SSP_RESPONSE_IU_T *) \
      ((char *)scic_sds_ssp_request_align_response_buffer ( \
         (char *)scic_sds_ssp_request_get_response_buffer_unaligned(memory) \
   )))

/**
 * This macro returns the address of the task context buffer in the io
 * request memory
 */
#define scic_sds_ssp_request_get_task_context_buffer_unaligned(memory) \
   ((SCU_TASK_CONTEXT_T *)( \
        ((char *)(scic_sds_ssp_request_get_response_buffer(memory))) \
      + sizeof(SCI_SSP_RESPONSE_IU_T) \
   ))

/**
 * This macro returns the aligned task context buffer
 */
#define scic_sds_ssp_request_get_task_context_buffer(memory) \
   ((SCU_TASK_CONTEXT_T *)( \
      ((char *)scic_sds_request_align_task_context_buffer( \
         (char *)scic_sds_ssp_request_get_task_context_buffer_unaligned(memory)) \
    )))

/**
 * This macro returns the address of the sgl elment pairs in the io request
 * memory buffer
 */
#define scic_sds_ssp_request_get_sgl_element_buffer(memory) \
   ((SCU_SGL_ELEMENT_PAIR_T *)( \
        ((char *)(scic_sds_ssp_request_get_task_context_buffer(memory))) \
      + sizeof(SCU_TASK_CONTEXT_T) \
    ))

#if !defined(DISABLE_TASK_MANAGEMENT)

/**
 * This macro returns the sizeof of memory required to store an SSP Task
 * request.  This does not include the size of the SCU Task Context memory.
 */
#define scic_ssp_task_request_get_object_size() \
   ( \
       sizeof(SCI_SSP_TASK_IU_T) \
     + sizeof(SCI_SSP_RESPONSE_IU_T) \
   )

/**
 * This macro returns the address of the ssp command buffer in the task
 * request memory.  Yes its the same as the above macro except for the
 * name.
 */
#define scic_sds_ssp_task_request_get_command_buffer(memory) \
   ((SCI_SSP_TASK_IU_T *)( \
        ((char *)(memory)) + sizeof(SCIC_SDS_REQUEST_T) \
   ))

/**
 * This macro returns the address of the ssp response buffer in the task
 * request memory.
 */
#define scic_sds_ssp_task_request_get_response_buffer(memory) \
   ((SCI_SSP_RESPONSE_IU_T *)( \
        ((char *)(scic_sds_ssp_task_request_get_command_buffer(memory))) \
      + sizeof(SCI_SSP_TASK_IU_T) \
   ))

/**
 * This macro returs the task context buffer for the SSP task request.
 */
#define scic_sds_ssp_task_request_get_task_context_buffer(memory) \
   ((SCU_TASK_CONTEXT_T *)( \
        ((char *)(scic_sds_ssp_task_request_get_response_buffer(memory))) \
      + sizeof(SCI_SSP_RESPONSE_IU_T) \
   ))

#endif // !defined(DISABLE_TASK_MANAGEMENT)


//****************************************************************************
//* SCIC SDS IO REQUEST PRIVATE METHODS
//****************************************************************************

#ifdef SCI_LOGGING
/**
 * This method will initialize state transition logging for the task request
 * object.
 *
 * @param[in] this_request This is the request for which to track state
 *       transitions.
 */
void scic_sds_request_initialize_state_logging(
   SCIC_SDS_REQUEST_T *this_request
)
{
   sci_base_state_machine_logger_initialize(
      &this_request->parent.state_machine_logger,
      &this_request->parent.state_machine,
      &this_request->parent.parent,
      scic_cb_logger_log_states,
      this_request->is_task_management_request ?
      "SCIC_SDS_IO_REQUEST_T(Task)" : "SCIC_SDS_IO_REQUEST_T(IO)",
      "base state machine",
      SCIC_LOG_OBJECT_SMP_IO_REQUEST |
      SCIC_LOG_OBJECT_STP_IO_REQUEST |
      SCIC_LOG_OBJECT_SSP_IO_REQUEST
   );

   if (this_request->has_started_substate_machine)
   {
      sci_base_state_machine_logger_initialize(
         &this_request->started_substate_machine_logger,
         &this_request->started_substate_machine,
         &this_request->parent.parent,
         scic_cb_logger_log_states,
         "SCIC_SDS_IO_REQUEST_T(Task)", "starting substate machine",
         SCIC_LOG_OBJECT_SMP_IO_REQUEST |
         SCIC_LOG_OBJECT_STP_IO_REQUEST |
         SCIC_LOG_OBJECT_SSP_IO_REQUEST
     );
   }
}

/**
 * This method will stop the state transition logging for the task request
 * object.
 *
 * @param[in] this_request The task request object on which to stop state
 *       transition logging.
 */
void scic_sds_request_deinitialize_state_logging(
   SCIC_SDS_REQUEST_T *this_request
)
{
   sci_base_state_machine_logger_deinitialize(
      &this_request->parent.state_machine_logger,
      &this_request->parent.state_machine
   );

   if (this_request->has_started_substate_machine)
   {
      sci_base_state_machine_logger_deinitialize(
         &this_request->started_substate_machine_logger,
         &this_request->started_substate_machine
      );
   }
}
#endif // SCI_LOGGING

/**
 * This method returns the size required to store an SSP IO request object.
 *
 * @return U32
 */
static
U32 scic_sds_ssp_request_get_object_size(void)
{
   return   sizeof(SCIC_SDS_REQUEST_T)
          + scic_ssp_io_request_get_object_size()
          + sizeof(SCU_TASK_CONTEXT_T)
          + CACHE_LINE_SIZE
          + sizeof(SCU_SGL_ELEMENT_PAIR_T) * SCU_MAX_SGL_ELEMENT_PAIRS;
}

/**
 * @brief This method returns the sgl element pair for the specificed
 *        sgl_pair index.
 *
 * @param[in] this_request This parameter specifies the IO request for which
 *            to retrieve the Scatter-Gather List element pair.
 * @param[in] sgl_pair_index This parameter specifies the index into the SGL
 *            element pair to be retrieved.
 *
 * @return This method returns a pointer to an SCU_SGL_ELEMENT_PAIR.
 */
SCU_SGL_ELEMENT_PAIR_T *scic_sds_request_get_sgl_element_pair(
   SCIC_SDS_REQUEST_T *this_request,
   U32                 sgl_pair_index
)
{
   SCU_TASK_CONTEXT_T *task_context;

   task_context = (SCU_TASK_CONTEXT_T *)this_request->task_context_buffer;

   if (sgl_pair_index == 0)
   {
      return &task_context->sgl_pair_ab;
   }
   else if (sgl_pair_index == 1)
   {
      return &task_context->sgl_pair_cd;
   }

   return &this_request->sgl_element_pair_buffer[sgl_pair_index - 2];
}

/**
 * @brief This function will build the SGL list for an IO request.
 *
 * @param[in] this_request This parameter specifies the IO request for which
 *            to build the Scatter-Gather List.
 *
 * @return none
 */
void scic_sds_request_build_sgl(
   SCIC_SDS_REQUEST_T *this_request
)
{
   void                   *os_sge;
   void                   *os_handle;
   SCI_PHYSICAL_ADDRESS    physical_address;
   U32                     sgl_pair_index = 0;
   SCU_SGL_ELEMENT_PAIR_T *scu_sgl_list   = NULL;
   SCU_SGL_ELEMENT_PAIR_T *previous_pair  = NULL;

   os_handle = scic_sds_request_get_user_request(this_request);
   scic_cb_io_request_get_next_sge(os_handle, NULL, &os_sge);

   while (os_sge != NULL)
   {
      scu_sgl_list =
         scic_sds_request_get_sgl_element_pair(this_request, sgl_pair_index);

      SCU_SGL_COPY(os_handle, scu_sgl_list->A, os_sge);

      scic_cb_io_request_get_next_sge(os_handle, os_sge, &os_sge);

      if (os_sge != NULL)
      {
         SCU_SGL_COPY(os_handle, scu_sgl_list->B, os_sge);

         scic_cb_io_request_get_next_sge(os_handle, os_sge, &os_sge);
      }
      else
      {
         SCU_SGL_ZERO(scu_sgl_list->B);
      }

      if (previous_pair != NULL)
      {
         scic_cb_io_request_get_physical_address(
            scic_sds_request_get_controller(this_request),
            this_request,
            scu_sgl_list,
            &physical_address
         );

         previous_pair->next_pair_upper =
            sci_cb_physical_address_upper(physical_address);
         previous_pair->next_pair_lower =
            sci_cb_physical_address_lower(physical_address);
      }

      previous_pair = scu_sgl_list;
      sgl_pair_index++;
   }

   if (scu_sgl_list != NULL)
   {
      scu_sgl_list->next_pair_upper = 0;
      scu_sgl_list->next_pair_lower = 0;
   }
}

/**
 * @brief This method initializes common portions of the io request object.
 *        This includes construction of the SCI_BASE_REQUEST_T parent.
 *
 * @param[in] the_controller This parameter specifies the controller for which
 *            the request is being constructed.
 * @param[in] the_target This parameter specifies the remote device for which
 *            the request is being constructed.
 * @param[in] io_tag This parameter specifies the IO tag to be utilized for
 *            this request.  This parameter can be set to
 *            SCI_CONTROLLER_INVALID_IO_TAG.
 * @param[in] user_io_request_object This parameter specifies the user
 *            request object for which the request is being constructed.
 * @param[in] this_request This parameter specifies the request being
 *            constructed.
 *
 * @return none
 */
static
void scic_sds_general_request_construct(
   SCIC_SDS_CONTROLLER_T    * the_controller,
   SCIC_SDS_REMOTE_DEVICE_T * the_target,
   U16                        io_tag,
   void                     * user_io_request_object,
   SCIC_SDS_REQUEST_T       * this_request
)
{
   sci_base_request_construct(
      &this_request->parent,
      sci_base_object_get_logger(the_controller),
      scic_sds_request_state_table
   );

   this_request->io_tag = io_tag;
   this_request->user_request = user_io_request_object;
   this_request->owning_controller = the_controller;
   this_request->target_device = the_target;
   this_request->has_started_substate_machine = FALSE;
   this_request->protocol = SCIC_NO_PROTOCOL;
   this_request->sat_protocol = 0xFF;
   this_request->saved_rx_frame_index = SCU_INVALID_FRAME_INDEX;
   this_request->device_sequence = scic_sds_remote_device_get_sequence(the_target);

   this_request->sci_status   = SCI_SUCCESS;
   this_request->scu_status   = 0;
   this_request->post_context = 0xFFFFFFFF;

   this_request->is_task_management_request = FALSE;

   if (io_tag == SCI_CONTROLLER_INVALID_IO_TAG)
   {
      this_request->was_tag_assigned_by_user = FALSE;
      this_request->task_context_buffer = NULL;
   }
   else
   {
      this_request->was_tag_assigned_by_user = TRUE;

      this_request->task_context_buffer =
         scic_sds_controller_get_task_context_buffer(
            this_request->owning_controller, io_tag);
   }
}

/**
 * @brief This method build the remainder of the IO request object.
 *
 * @pre The scic_sds_general_request_construct() must be called before this
 *      call is valid.
 *
 * @param[in] this_request This parameter specifies the request object being
 *            constructed.
 *
 * @return none
 */
void scic_sds_ssp_io_request_assign_buffers(
   SCIC_SDS_REQUEST_T *this_request
)
{
   this_request->command_buffer =
      scic_sds_ssp_request_get_command_buffer(this_request);
   this_request->response_buffer =
      scic_sds_ssp_request_get_response_buffer(this_request);
   this_request->sgl_element_pair_buffer =
      scic_sds_ssp_request_get_sgl_element_buffer(this_request);
   this_request->sgl_element_pair_buffer =
      scic_sds_request_align_sgl_element_buffer(this_request->sgl_element_pair_buffer);

   if (this_request->was_tag_assigned_by_user == FALSE)
   {
      this_request->task_context_buffer =
         scic_sds_ssp_request_get_task_context_buffer(this_request);
   }
}

/**
 * @brief This method constructs the SSP Command IU data for this io
 *        request object.
 *
 * @param[in] this_request This parameter specifies the request object for
 *            which the SSP command information unit is being built.
 *
 * @return none
 */
static
void scic_sds_io_request_build_ssp_command_iu(
   SCIC_SDS_REQUEST_T   *this_request
)
{
   SCI_SINGLE_LEVEL_LUN_T lun;
   SCI_SSP_COMMAND_IU_T *command_frame;
   void                 *os_handle;
   U32  cdb_length;
   U32 *cdb_buffer;

   command_frame =
      (SCI_SSP_COMMAND_IU_T *)this_request->command_buffer;

   os_handle = scic_sds_request_get_user_request(this_request);

   ((U32 *)&lun)[0] = 0;
   ((U32 *)&lun)[1] = 0;
   lun.lun_number = scic_cb_ssp_io_request_get_lun(os_handle) &0xff;
   /// @todo Is it ok to leave junk at the end of the cdb buffer?
   scic_word_copy_with_swap(
       (U32 *)command_frame->lun,
       (U32 *)&lun,
       sizeof(lun));

   ((U32 *)command_frame)[2] = 0;

   cdb_length = scic_cb_ssp_io_request_get_cdb_length(os_handle);
   cdb_buffer = (U32 *)scic_cb_ssp_io_request_get_cdb_address(os_handle);

   if (cdb_length > 16)
   {
      command_frame->additional_cdb_length = cdb_length - 16;
   }

   /// @todo Is it ok to leave junk at the end of the cdb buffer?
   scic_word_copy_with_swap(
      (U32 *)(&command_frame->cdb),
      (U32 *)(cdb_buffer),
      (cdb_length + 3) / sizeof(U32)
   );

   command_frame->enable_first_burst = 0;
   command_frame->task_priority =
      scic_cb_ssp_io_request_get_command_priority(os_handle);
   command_frame->task_attribute =
      scic_cb_ssp_io_request_get_task_attribute(os_handle);
}

#if !defined(DISABLE_TASK_MANAGEMENT)

/**
 * @brief This method constructs the SSP Task IU data for this io request
 *        object.
 *
 * @param[in] this_request
 *
 * @return none
 */
static
void scic_sds_task_request_build_ssp_task_iu(
   SCIC_SDS_REQUEST_T *this_request
)
{
   SCI_SSP_TASK_IU_T *command_frame;
   void              *os_handle;

   command_frame =
      (SCI_SSP_TASK_IU_T *)this_request->command_buffer;

   os_handle = scic_sds_request_get_user_request(this_request);

   command_frame->lun_upper = 0;
   command_frame->lun_lower = scic_cb_ssp_task_request_get_lun(os_handle);

   ((U32 *)command_frame)[2] = 0;

   command_frame->task_function =
      scic_cb_ssp_task_request_get_function(os_handle);
   command_frame->task_tag =
      scic_cb_ssp_task_request_get_io_tag_to_manage(os_handle);
}

#endif // !defined(DISABLE_TASK_MANAGEMENT)

/**
 * @brief This method is will fill in the SCU Task Context for any type of
 *        SSP request.
 *
 * @param[in] this_request
 * @param[in] task_context
 *
 * @return none
 */
static
void scu_ssp_reqeust_construct_task_context(
   SCIC_SDS_REQUEST_T * this_request,
   SCU_TASK_CONTEXT_T * task_context
)
{
   SCI_PHYSICAL_ADDRESS      physical_address;
   SCIC_SDS_CONTROLLER_T    *owning_controller;
   SCIC_SDS_REMOTE_DEVICE_T *target_device;
   SCIC_SDS_PORT_T          *target_port;

   owning_controller = scic_sds_request_get_controller(this_request);
   target_device = scic_sds_request_get_device(this_request);
   target_port = scic_sds_request_get_port(this_request);

   // Fill in the TC with the its required data
   task_context->abort = 0;
   task_context->priority = 0;
   task_context->initiator_request = 1;
   task_context->connection_rate =
      scic_remote_device_get_connection_rate(target_device);
   task_context->protocol_engine_index =
      scic_sds_controller_get_protocol_engine_group(owning_controller);
   task_context->logical_port_index =
      scic_sds_port_get_index(target_port);
   task_context->protocol_type = SCU_TASK_CONTEXT_PROTOCOL_SSP;
   task_context->valid = SCU_TASK_CONTEXT_VALID;
   task_context->context_type = SCU_TASK_CONTEXT_TYPE;

   task_context->remote_node_index =
      scic_sds_remote_device_get_index(this_request->target_device);
   task_context->command_code = 0;

   task_context->link_layer_control = 0;
   task_context->do_not_dma_ssp_good_response = 1;
   task_context->strict_ordering = 0;
   task_context->control_frame = 0;
   task_context->timeout_enable = 0;
   task_context->block_guard_enable = 0;

   task_context->address_modifier = 0;

   //task_context->type.ssp.tag = this_request->io_tag;
   task_context->task_phase = 0x01;

   if (this_request->was_tag_assigned_by_user)
   {
      // Build the task context now since we have already read the data
      this_request->post_context = (
           SCU_CONTEXT_COMMAND_REQUEST_TYPE_POST_TC
         | (
                scic_sds_controller_get_protocol_engine_group(owning_controller)
             << SCU_CONTEXT_COMMAND_PROTOCOL_ENGINE_GROUP_SHIFT
           )
         | (
                 scic_sds_port_get_index(target_port)
              << SCU_CONTEXT_COMMAND_LOGICAL_PORT_SHIFT
           )
         | scic_sds_io_tag_get_index(this_request->io_tag)
      );
   }
   else
   {
      // Build the task context now since we have already read the data
      this_request->post_context = (
           SCU_CONTEXT_COMMAND_REQUEST_TYPE_POST_TC
         | (
               scic_sds_controller_get_protocol_engine_group(owning_controller)
            << SCU_CONTEXT_COMMAND_PROTOCOL_ENGINE_GROUP_SHIFT
           )
         | (
                scic_sds_port_get_index(target_port)
             << SCU_CONTEXT_COMMAND_LOGICAL_PORT_SHIFT
           )
         // This is not assigned because we have to wait until we get a TCi
      );
   }

   // Copy the physical address for the command buffer to the SCU Task Context
   scic_cb_io_request_get_physical_address(
      scic_sds_request_get_controller(this_request),
      this_request,
      this_request->command_buffer,
      &physical_address
   );

   task_context->command_iu_upper =
      sci_cb_physical_address_upper(physical_address);
   task_context->command_iu_lower =
      sci_cb_physical_address_lower(physical_address);

   // Copy the physical address for the response buffer to the SCU Task Context
   scic_cb_io_request_get_physical_address(
      scic_sds_request_get_controller(this_request),
      this_request,
      this_request->response_buffer,
      &physical_address
   );

   task_context->response_iu_upper =
      sci_cb_physical_address_upper(physical_address);
   task_context->response_iu_lower =
      sci_cb_physical_address_lower(physical_address);
}

/**
 * @brief This method is will fill in the SCU Task Context for a SSP IO
 *        request.
 *
 * @param[in] this_request
 *
 * @return none
 */
static
void scu_ssp_io_request_construct_task_context(
   SCIC_SDS_REQUEST_T *this_request,
   SCI_IO_REQUEST_DATA_DIRECTION data_direction,
   U32 transfer_length_bytes
)
{
   SCU_TASK_CONTEXT_T *task_context;

   task_context = scic_sds_request_get_task_context(this_request);

   scu_ssp_reqeust_construct_task_context(this_request, task_context);

   task_context->ssp_command_iu_length = sizeof(SCI_SSP_COMMAND_IU_T) / sizeof(U32);
   task_context->type.ssp.frame_type = SCI_SAS_COMMAND_FRAME;

   switch (data_direction)
   {
   case SCI_IO_REQUEST_DATA_IN:
   case SCI_IO_REQUEST_NO_DATA:
      task_context->task_type = SCU_TASK_TYPE_IOREAD;
      break;
   case SCI_IO_REQUEST_DATA_OUT:
      task_context->task_type = SCU_TASK_TYPE_IOWRITE;
      break;
   }

   task_context->transfer_length_bytes = transfer_length_bytes;

   if (task_context->transfer_length_bytes > 0)
   {
      scic_sds_request_build_sgl(this_request);
   }
}

#if !defined(DISABLE_TASK_MANAGEMENT)

/**
 * @brief This method will fill in the remainder of the io request object
 *        for SSP Task requests.
 *
 * @param[in] this_request
 *
 * @return none
 */
void scic_sds_ssp_task_request_assign_buffers(
   SCIC_SDS_REQUEST_T *this_request
)
{
   // Assign all of the buffer pointers
   this_request->command_buffer =
      scic_sds_ssp_task_request_get_command_buffer(this_request);
   this_request->response_buffer =
      scic_sds_ssp_task_request_get_response_buffer(this_request);
   this_request->sgl_element_pair_buffer = NULL;

   if (this_request->was_tag_assigned_by_user == FALSE)
   {
      this_request->task_context_buffer =
         scic_sds_ssp_task_request_get_task_context_buffer(this_request);
      this_request->task_context_buffer =
         scic_sds_request_align_task_context_buffer(this_request->task_context_buffer);
   }
}

/**
 * @brief This method will fill in the SCU Task Context for a SSP Task
 *        request.  The following important settings are utilized:
 *          -# priority == SCU_TASK_PRIORITY_HIGH.  This ensures that the
 *             task request is issued ahead of other task destined for the
 *             same Remote Node.
 *          -# task_type == SCU_TASK_TYPE_IOREAD.  This simply indicates
 *             that a normal request type (i.e. non-raw frame) is being
 *             utilized to perform task management.
 *          -# control_frame == 1.  This ensures that the proper endianness
 *             is set so that the bytes are transmitted in the right order
 *             for a task frame.
 *
 * @param[in] this_request This parameter specifies the task request object
 *            being constructed.
 *
 * @return none
 */
static
void scu_ssp_task_request_construct_task_context(
   SCIC_SDS_REQUEST_T *this_request
)
{
   SCU_TASK_CONTEXT_T *task_context;

   task_context = scic_sds_request_get_task_context(this_request);

   scu_ssp_reqeust_construct_task_context(this_request, task_context);

   task_context->control_frame                = 1;
   task_context->priority                     = SCU_TASK_PRIORITY_HIGH;
   task_context->task_type                    = SCU_TASK_TYPE_RAW_FRAME;
   task_context->transfer_length_bytes        = 0;
   task_context->type.ssp.frame_type          = SCI_SAS_TASK_FRAME;
   task_context->ssp_command_iu_length = sizeof(SCI_SSP_TASK_IU_T) / sizeof(U32);
}

#endif // !defined(DISABLE_TASK_MANAGEMENT)

#if !defined(DISABLE_PASS_THROUGH)
/**
 * @brief This method constructs the SSP Command IU data for this
 *        ssp passthrough comand request object.
 *
 * @param[in] this_request This parameter specifies the request object for
 *            which the SSP command information unit is being built.
 *
 * @return SCI_STATUS, returns invalid parameter is cdb > 16
 */
static
SCI_STATUS scic_sds_io_request_build_ssp_command_iu_pass_through(
   SCIC_SDS_REQUEST_T   *this_request,
   SCIC_SSP_PASSTHRU_REQUEST_CALLBACKS_T *ssp_passthru_cb
)
{
   SCI_SSP_COMMAND_IU_T *command_frame;
   U32  cdb_length = 0, additional_cdb_length = 0;
   U8 *cdb_buffer, *additional_cdb_buffer;
   U8 *scsi_lun;
   SCI_STATUS sci_status = SCI_SUCCESS;
   SCI_SINGLE_LEVEL_LUN_T lun;

   command_frame =
      (SCI_SSP_COMMAND_IU_T *)this_request->command_buffer;

   //get the lun
   ssp_passthru_cb->scic_cb_ssp_passthru_get_lun (
      this_request,
     &scsi_lun
   );
   memset(&lun, 0, sizeof(lun));
   lun.lun_number = *scsi_lun;
   scic_word_copy_with_swap(
       (U32 *)command_frame->lun,
       (U32 *)&lun,
       sizeof(lun));

   ((U32 *)command_frame)[2] = 0;

   ssp_passthru_cb->scic_cb_ssp_passthru_get_cdb(
      this_request,
     &cdb_length,
     &cdb_buffer,
     &additional_cdb_length,
     &additional_cdb_buffer
   );

   command_frame->additional_cdb_length = additional_cdb_length;

   // ----------- TODO
   ///todo: what to do with additional cdb length and buffer as the current command buffer is
   // 16 bytes in intel_sas.h
   // ??? see the SAS command IU
   if (additional_cdb_length > 0)
   {
     return SCI_FAILURE_INVALID_PARAMETER_VALUE;
   }

   /// @todo Is it ok to leave junk at the end of the cdb buffer?
   scic_word_copy_with_swap(
      (U32 *)(&command_frame->cdb),
      (U32 *)(cdb_buffer),
      (cdb_length + 3) / sizeof(U32)
   );

   /////-------- End fo TODO

   command_frame->enable_first_burst = 0;
   command_frame->task_priority = 0;  //todo: check with Richard ????

   //get the task attribute
   command_frame->task_attribute = ssp_passthru_cb->scic_cb_ssp_passthru_get_task_attribute (
                                      this_request
                             );

   return sci_status;
}
#endif // !defined(DISABLE_PASS_THROUGH)

//****************************************************************************
//* SCIC Interface Implementation
//****************************************************************************

#if !defined(DISABLE_TASK_MANAGEMENT)
/**
 * This method returns the size required to store an SSP task request object.
 *
 * @return U32
 */
static
U32 scic_sds_ssp_task_request_get_object_size(void)
{
   return   sizeof(SCIC_SDS_REQUEST_T)
          + scic_ssp_task_request_get_object_size()
          + sizeof(SCU_TASK_CONTEXT_T)
          + CACHE_LINE_SIZE;
}


U32 scic_task_request_get_object_size(void)
{
   U32 ssp_task_request_size;
   U32 stp_task_request_size;

   ssp_task_request_size = scic_sds_ssp_task_request_get_object_size();
   stp_task_request_size = scic_sds_stp_task_request_get_object_size();

   return MAX(ssp_task_request_size, stp_task_request_size);
}

#endif // !defined(DISABLE_TASK_MANAGEMENT)

// ---------------------------------------------------------------------------

U32 scic_io_request_get_object_size(void)
{
   U32 ssp_request_size;
   U32 stp_request_size;
   U32 smp_request_size;

   ssp_request_size = scic_sds_ssp_request_get_object_size();
   stp_request_size = scic_sds_stp_request_get_object_size();
   smp_request_size = scic_sds_smp_request_get_object_size();

   return MAX(ssp_request_size, MAX(stp_request_size, smp_request_size));
}

// ---------------------------------------------------------------------------

SCIC_TRANSPORT_PROTOCOL scic_io_request_get_protocol(
   SCI_IO_REQUEST_HANDLE_T  scic_io_request
)
{
   SCIC_SDS_REQUEST_T * this_request = (SCIC_SDS_REQUEST_T * )scic_io_request;
   return this_request->protocol;
}

// ---------------------------------------------------------------------------

U32 scic_sds_request_get_min_timer_count(void)
{
   return SCIC_SDS_IO_REQUEST_MINIMUM_TIMER_COUNT;
}

// ---------------------------------------------------------------------------

U32 scic_sds_request_get_max_timer_count(void)
{
   return SCIC_SDS_IO_REQUEST_MAXIMUM_TIMER_COUNT;
}

// ---------------------------------------------------------------------------

SCI_STATUS scic_io_request_construct(
   SCI_CONTROLLER_HANDLE_T      scic_controller,
   SCI_REMOTE_DEVICE_HANDLE_T   scic_remote_device,
   U16                          io_tag,
   void                       * user_io_request_object,
   void                       * scic_io_request_memory,
   SCI_IO_REQUEST_HANDLE_T    * new_scic_io_request_handle
)
{
   SCI_STATUS                          status = SCI_SUCCESS;
   SCIC_SDS_REQUEST_T                * this_request;
   SMP_DISCOVER_RESPONSE_PROTOCOLS_T   device_protocol;

   this_request = (SCIC_SDS_REQUEST_T * )scic_io_request_memory;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(scic_controller),
      (SCIC_LOG_OBJECT_SSP_IO_REQUEST
      |SCIC_LOG_OBJECT_SMP_IO_REQUEST
      |SCIC_LOG_OBJECT_STP_IO_REQUEST),
      "scic_io_request_construct(0x%x, 0x%x, 0x02x, 0x%x, 0x%x, 0x%x) enter\n",
      scic_controller, scic_remote_device,
      io_tag, user_io_request_object,
      this_request, new_scic_io_request_handle
   ));

   // Build the common part of the request
   scic_sds_general_request_construct(
      (SCIC_SDS_CONTROLLER_T *)scic_controller,
      (SCIC_SDS_REMOTE_DEVICE_T *)scic_remote_device,
      io_tag,
      user_io_request_object,
      this_request
   );

   if (
         scic_sds_remote_device_get_index((SCIC_SDS_REMOTE_DEVICE_T *)scic_remote_device)
      == SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX
      )
   {
      return SCI_FAILURE_INVALID_REMOTE_DEVICE;
   }

   scic_remote_device_get_protocols(scic_remote_device, &device_protocol);

   if (device_protocol.u.bits.attached_ssp_target)
   {
      scic_sds_ssp_io_request_assign_buffers(this_request);
   }
   else if (device_protocol.u.bits.attached_stp_target)
   {
      scic_sds_stp_request_assign_buffers(this_request);
      memset(this_request->command_buffer, 0, sizeof(SATA_FIS_REG_H2D_T));
   }
   else if (device_protocol.u.bits.attached_smp_target)
   {
      scic_sds_smp_request_assign_buffers(this_request);
      memset(this_request->command_buffer, 0, sizeof(SMP_REQUEST_T));
   }
   else
   {
      status = SCI_FAILURE_UNSUPPORTED_PROTOCOL;
   }

   if (status == SCI_SUCCESS)
   {
      memset(
         this_request->task_context_buffer,
         0,
         SCI_FIELD_OFFSET(SCU_TASK_CONTEXT_T, sgl_pair_ab)
      );
      *new_scic_io_request_handle = scic_io_request_memory;
   }

   return status;
}

// ---------------------------------------------------------------------------

#if !defined(DISABLE_TASK_MANAGEMENT)

SCI_STATUS scic_task_request_construct(
   SCI_CONTROLLER_HANDLE_T     controller,
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device,
   U16                         io_tag,
   void                       *user_io_request_object,
   void                       *scic_task_request_memory,
   SCI_TASK_REQUEST_HANDLE_T  *new_scic_task_request_handle
)
{
   SCI_STATUS           status = SCI_SUCCESS;
   SCIC_SDS_REQUEST_T * this_request = (SCIC_SDS_REQUEST_T *)
                                       scic_task_request_memory;
   SMP_DISCOVER_RESPONSE_PROTOCOLS_T   device_protocol;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(controller),
      (SCIC_LOG_OBJECT_SSP_IO_REQUEST
      |SCIC_LOG_OBJECT_SMP_IO_REQUEST
      |SCIC_LOG_OBJECT_STP_IO_REQUEST),
      "scic_task_request_construct(0x%x, 0x%x, 0x02x, 0x%x, 0x%x, 0x%x) enter\n",
      controller, remote_device,
      io_tag, user_io_request_object,
      scic_task_request_memory, new_scic_task_request_handle
   ));

   // Build the common part of the request
   scic_sds_general_request_construct(
      (SCIC_SDS_CONTROLLER_T *)controller,
      (SCIC_SDS_REMOTE_DEVICE_T *)remote_device,
      io_tag,
      user_io_request_object,
      this_request
   );

   scic_remote_device_get_protocols(remote_device, &device_protocol);

   if (device_protocol.u.bits.attached_ssp_target)
   {
      scic_sds_ssp_task_request_assign_buffers(this_request);

      this_request->has_started_substate_machine = TRUE;

      // Construct the started sub-state machine.
      sci_base_state_machine_construct(
         &this_request->started_substate_machine,
         &this_request->parent.parent,
         scic_sds_io_request_started_task_mgmt_substate_table,
         SCIC_SDS_IO_REQUEST_STARTED_TASK_MGMT_SUBSTATE_AWAIT_TC_COMPLETION
      );
   }
   else if (device_protocol.u.bits.attached_stp_target)
   {
      scic_sds_stp_request_assign_buffers(this_request);
   }
   else
   {
      status = SCI_FAILURE_UNSUPPORTED_PROTOCOL;
   }

   if (status == SCI_SUCCESS)
   {
      this_request->is_task_management_request = TRUE;
      memset(this_request->task_context_buffer, 0x00, sizeof(SCU_TASK_CONTEXT_T));
      *new_scic_task_request_handle            = scic_task_request_memory;
   }

   return status;
}

#endif // !defined(DISABLE_TASK_MANAGEMENT)

// ---------------------------------------------------------------------------

SCI_STATUS scic_io_request_construct_basic_ssp(
   SCI_IO_REQUEST_HANDLE_T  scic_io_request
)
{
   void               *os_handle;
   SCIC_SDS_REQUEST_T *this_request;
   this_request = (SCIC_SDS_REQUEST_T *)scic_io_request;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_request),
      SCIC_LOG_OBJECT_SSP_IO_REQUEST,
      "scic_io_request_construct_basic_ssp(0x%x) enter\n",
      this_request
   ));

   this_request->protocol = SCIC_SSP_PROTOCOL;

   os_handle = scic_sds_request_get_user_request(this_request);

   scu_ssp_io_request_construct_task_context(
      this_request,
      scic_cb_io_request_get_data_direction(os_handle),
      scic_cb_io_request_get_transfer_length(os_handle)
   );


   scic_sds_io_request_build_ssp_command_iu(this_request);

   scic_sds_request_initialize_state_logging(this_request);

   sci_base_state_machine_change_state(
      &this_request->parent.state_machine,
      SCI_BASE_REQUEST_STATE_CONSTRUCTED
   );

   return SCI_SUCCESS;
}

// ---------------------------------------------------------------------------

#if !defined(DISABLE_TASK_MANAGEMENT)

SCI_STATUS scic_task_request_construct_ssp(
   SCI_TASK_REQUEST_HANDLE_T  scic_task_request
)
{
   SCIC_SDS_REQUEST_T *this_request = (SCIC_SDS_REQUEST_T *)
                                      scic_task_request;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_request),
      SCIC_LOG_OBJECT_SSP_IO_REQUEST,
      "scic_task_request_construct_ssp(0x%x) enter\n",
      this_request
   ));

   // Construct the SSP Task SCU Task Context
   scu_ssp_task_request_construct_task_context(this_request);

   // Fill in the SSP Task IU
   scic_sds_task_request_build_ssp_task_iu(this_request);

   scic_sds_request_initialize_state_logging(this_request);

   sci_base_state_machine_change_state(
      &this_request->parent.state_machine,
      SCI_BASE_REQUEST_STATE_CONSTRUCTED
   );

   return SCI_SUCCESS;
}

#endif // !defined(DISABLE_TASK_MANAGEMENT)

// ---------------------------------------------------------------------------

SCI_STATUS scic_io_request_construct_advanced_ssp(
   SCI_IO_REQUEST_HANDLE_T    scic_io_request,
   SCIC_IO_SSP_PARAMETERS_T * io_parameters
)
{
   SCIC_LOG_TRACE((
      sci_base_object_get_logger(scic_io_request),
      SCIC_LOG_OBJECT_SSP_IO_REQUEST,
      "scic_io_request_construct_advanced_ssp(0x%x, 0x%x) enter\n",
      io_parameters, scic_io_request
   ));

   /// @todo Implement after 1.1
   return SCI_FAILURE;
}

// ---------------------------------------------------------------------------

#if !defined(DISABLE_PASS_THROUGH)
SCI_STATUS scic_io_request_construct_ssp_pass_through (
   void                    * scic_io_request,
   SCIC_SSP_PASSTHRU_REQUEST_CALLBACKS_T *ssp_passthru_cb
)
{
   SCI_STATUS               status = SCI_SUCCESS;
   SCIC_SDS_REQUEST_T       * this_request;

   this_request = (SCIC_SDS_REQUEST_T * )scic_io_request;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(scic_io_request),
      SCIC_LOG_OBJECT_STP_IO_REQUEST,
      "scic_io_request_construct_ssp_pass_through(0x%x) enter\n",
      scic_io_request
   ));

   //build the task context from the pass through buffer
   scu_ssp_io_request_construct_task_context(
      this_request,
      ssp_passthru_cb->common_callbacks.scic_cb_passthru_get_data_direction (this_request),
      ssp_passthru_cb->common_callbacks.scic_cb_passthru_get_transfer_length(this_request)
   );

   //build the ssp command iu from the pass through buffer
   status = scic_sds_io_request_build_ssp_command_iu_pass_through (
               this_request,
               ssp_passthru_cb
            );
   if (status != SCI_SUCCESS)
   {
      return status;
   }

   /* initialize the logging */
   scic_sds_request_initialize_state_logging(this_request);

   sci_base_state_machine_change_state(
      &this_request->parent.state_machine,
      SCI_BASE_REQUEST_STATE_CONSTRUCTED
   );

   return status;
}
#endif // !defined(DISABLE_PASS_THROUGH)

// ---------------------------------------------------------------------------

#if !defined(DISABLE_TASK_MANAGEMENT)

SCI_STATUS scic_task_request_construct_sata(
   SCI_TASK_REQUEST_HANDLE_T scic_task_request
)
{
   SCI_STATUS           status;
   SCIC_SDS_REQUEST_T * this_request;
   U8                   sat_protocol;

   this_request = (SCIC_SDS_REQUEST_T *)scic_task_request;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_request),
      SCIC_LOG_OBJECT_STP_IO_REQUEST,
      "scic_task_request_construct_sata(0x%x) enter\n",
      this_request
   ));

   sat_protocol =
      scic_cb_request_get_sat_protocol(this_request->user_request);

   this_request->sat_protocol = sat_protocol;

   switch (sat_protocol)
   {
   case SAT_PROTOCOL_ATA_HARD_RESET:
   case SAT_PROTOCOL_SOFT_RESET:
      status = scic_sds_stp_soft_reset_request_construct(this_request);
      break;

   case SAT_PROTOCOL_PIO_DATA_IN:
      status = scic_sds_stp_pio_request_construct(this_request, sat_protocol, FALSE);
      break;

   default:
      SCIC_LOG_ERROR((
         sci_base_object_get_logger(this_request),
         SCIC_LOG_OBJECT_STP_IO_REQUEST,
         "SCIC IO Request 0x%x received un-handled SAT Protocl %d.\n",
         this_request, sat_protocol
      ));

      status = SCI_FAILURE;
      break;
   }

   if (status == SCI_SUCCESS)
   {
      scic_sds_request_initialize_state_logging(this_request);

      sci_base_state_machine_change_state(
         &this_request->parent.state_machine,
         SCI_BASE_REQUEST_STATE_CONSTRUCTED
      );
   }

   return status;
}

#endif // !defined(DISABLE_TASK_MANAGEMENT)

// ---------------------------------------------------------------------------

#if !defined(DISABLE_PASS_THROUGH)
SCI_STATUS scic_io_request_construct_sata_pass_through(
   SCI_IO_REQUEST_HANDLE_T scic_io_request,
   SCIC_STP_PASSTHRU_REQUEST_CALLBACKS_T *passthru_cb
)
{
   SCI_STATUS                       status = SCI_SUCCESS;
   SCIC_SDS_REQUEST_T               * this_request;
   U8                               sat_protocol;
   U8                               * reg_fis;
   U32                              transfer_length;
   SCI_IO_REQUEST_DATA_DIRECTION    data_direction;

   this_request = (SCIC_SDS_REQUEST_T * )scic_io_request;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(scic_io_request),
      SCIC_LOG_OBJECT_STP_IO_REQUEST,
      "scic_io_request_construct_sata_pass_through(0x%x) enter\n",
      scic_io_request
   ));

   passthru_cb->scic_cb_stp_passthru_get_register_fis(this_request, &reg_fis);

   if (reg_fis == NULL)
   {
      status = SCI_FAILURE_INVALID_PARAMETER_VALUE;
   }

   if (status == SCI_SUCCESS)
   {
      //copy the H2D Reg fis blindly from the request to the SCU command buffer
      memcpy ((U8 *)this_request->command_buffer, (U8 *)reg_fis, sizeof(SATA_FIS_REG_H2D_T));

      //continue to create the request
      sat_protocol = passthru_cb->scic_cb_stp_passthru_get_protocol(this_request);
      transfer_length = passthru_cb->common_callbacks.scic_cb_passthru_get_transfer_length(this_request);
      data_direction = passthru_cb->common_callbacks.scic_cb_passthru_get_data_direction(this_request);

      status = scic_sds_io_request_construct_sata(
                  this_request,
                  sat_protocol,
                  transfer_length,
                  data_direction,
                  TRUE,
                  TRUE
               );

      this_request->protocol = SCIC_STP_PROTOCOL;
   }

   return status;
}
#endif // !defined(DISABLE_PASS_THROUGH)

// ---------------------------------------------------------------------------

U16 scic_io_request_get_io_tag(
   SCI_IO_REQUEST_HANDLE_T  scic_io_request
)
{
   SCIC_SDS_REQUEST_T *this_request;
   this_request = (SCIC_SDS_REQUEST_T *)scic_io_request;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(scic_io_request),
      SCIC_LOG_OBJECT_SMP_IO_REQUEST,
      "scic_io_request_get_io_tag(0x%x) enter\n",
      scic_io_request
   ));

   return this_request->io_tag;
}

// ---------------------------------------------------------------------------

U32 scic_request_get_controller_status(
   SCI_IO_REQUEST_HANDLE_T  io_request
)
{
   SCIC_SDS_REQUEST_T * this_request = (SCIC_SDS_REQUEST_T*)io_request;
   return this_request->scu_status;
}

U32 scic_request_get_sci_status(
   SCI_IO_REQUEST_HANDLE_T  io_request
)
{
   SCIC_SDS_REQUEST_T * this_request = (SCIC_SDS_REQUEST_T*)io_request;
   return this_request->sci_status;
}

// ---------------------------------------------------------------------------

void * scic_io_request_get_rx_frame(
   SCI_IO_REQUEST_HANDLE_T  scic_io_request,
   U32                      offset
)
{
   void               * frame_buffer = NULL;
   SCIC_SDS_REQUEST_T * this_request = (SCIC_SDS_REQUEST_T *)scic_io_request;

   ASSERT(offset < SCU_UNSOLICITED_FRAME_BUFFER_SIZE);

   if (this_request->saved_rx_frame_index != SCU_INVALID_FRAME_INDEX)
   {
      scic_sds_unsolicited_frame_control_get_buffer(
         &(this_request->owning_controller->uf_control),
         this_request->saved_rx_frame_index,
         &frame_buffer
      );
   }

   return frame_buffer;
}

void * scic_io_request_get_command_iu_address(
   SCI_IO_REQUEST_HANDLE_T  scic_io_request
)
{
   SCIC_SDS_REQUEST_T * this_request = (SCIC_SDS_REQUEST_T *)scic_io_request;

   return this_request->command_buffer;
}

// ---------------------------------------------------------------------------

void * scic_io_request_get_response_iu_address(
   SCI_IO_REQUEST_HANDLE_T scic_io_request
)
{
   SCIC_SDS_REQUEST_T * this_request = (SCIC_SDS_REQUEST_T *)scic_io_request;

   return this_request->response_buffer;
}

// ---------------------------------------------------------------------------
#define SCU_TASK_CONTEXT_SRAM 0x200000
U32 scic_io_request_get_number_of_bytes_transferred (
   SCI_IO_REQUEST_HANDLE_T  scic_io_request
)
{
   U32 ret_val = 0;
   SCIC_SDS_REQUEST_T       * scic_sds_request;

   scic_sds_request = (SCIC_SDS_REQUEST_T *) scic_io_request;

   if ( SMU_AMR_READ (scic_sds_request->owning_controller) == 0)
   {
      //get the bytes of data from the Address == BAR1 + 20002Ch + (256*TCi) where
      //   BAR1 is the scu_registers
      //   0x20002C = 0x200000 + 0x2c
      //            = start of task context SRAM + offset of (type.ssp.data_offset)
      //   TCi is the io_tag of SCIC_SDS_REQUEST
      ret_val =  scic_sds_pci_read_scu_dword(
                    scic_sds_request->owning_controller,
                    (
                       (U8 *) scic_sds_request->owning_controller->scu_registers +
                          ( SCU_TASK_CONTEXT_SRAM + SCI_FIELD_OFFSET(SCU_TASK_CONTEXT_T, type.ssp.data_offset) ) +
                       ( ( sizeof (SCU_TASK_CONTEXT_T) ) * scic_sds_io_tag_get_index (scic_sds_request->io_tag))
                    )
                 );
   }

   return ret_val;
}

//****************************************************************************
//* SCIC SDS Interface Implementation
//****************************************************************************

/**
 * This method invokes the base state start request handler for the
 * SCIC_SDS_IO_REQUEST_T object.
 *
 * @param[in] this_request The SCIC_SDS_IO_REQUEST_T object for which the
 *       start operation is to be executed.
 *
 * @return SCI_STATUS
 */
SCI_STATUS scic_sds_request_start(
   SCIC_SDS_REQUEST_T *this_request
)
{
   if (
         this_request->device_sequence
      == scic_sds_remote_device_get_sequence(this_request->target_device)
      )
   {
      return this_request->state_handlers->parent.start_handler(
                &this_request->parent
             );
   }

   return SCI_FAILURE;
}

/**
 * This method invokes the base state terminate request handber for the
 * SCIC_SDS_IO_REQUEST_T object.
 *
 * @param[in] this_request The SCIC_SDS_IO_REQUEST_T object for which the
 *       start operation is to be executed.
 *
 * @return SCI_STATUS
 */
SCI_STATUS scic_sds_io_request_terminate(
   SCIC_SDS_REQUEST_T *this_request
)
{
   return this_request->state_handlers->parent.abort_handler(
                                                      &this_request->parent);
}

/**
 * This method invokes the base state request completion handler for the
 * SCIC_SDS_IO_REQUEST_T object.
 *
 * @param[in] this_request The SCIC_SDS_IO_REQUEST_T object for which the
 *       start operation is to be executed.
 *
 * @return SCI_STATUS
 */
SCI_STATUS scic_sds_io_request_complete(
   SCIC_SDS_REQUEST_T *this_request
)
{
   return this_request->state_handlers->parent.complete_handler(
                                                      &this_request->parent);
}

/**
 * This method invokes the core state handler for the SCIC_SDS_IO_REQUEST_T
 * object.
 *
 * @param[in] this_request The SCIC_SDS_IO_REQUEST_T object for which the
 *       start operation is to be executed.
 * @param[in] event_code The event code returned by the hardware for the task
 *       reqeust.
 *
 * @return SCI_STATUS
 */
SCI_STATUS scic_sds_io_request_event_handler(
   SCIC_SDS_REQUEST_T * this_request,
   U32                  event_code
)
{
   return this_request->state_handlers->event_handler(this_request, event_code);
}

/**
 * This method invokes the core state frame handler for the
 * SCIC_SDS_IO_REQUEST_T object.
 *
 * @param[in] this_request The SCIC_SDS_IO_REQUEST_T object for which the
 *       start operation is to be executed.
 * @param[in] frame_index The frame index returned by the hardware for the
 *       reqeust object.
 *
 * @return SCI_STATUS
 */
SCI_STATUS scic_sds_io_request_frame_handler(
   SCIC_SDS_REQUEST_T * this_request,
   U32                  frame_index
)
{
   return this_request->state_handlers->frame_handler(this_request, frame_index);
}

/**
 * This method invokes the core state task complete handler for the
 * SCIC_SDS_IO_REQUEST_T object.
 *
 * @param[in] this_request The SCIC_SDS_IO_REQUEST_T object for which the task
 *       start operation is to be executed.
 *
 * @return SCI_STATUS
 */
SCI_STATUS scic_sds_task_request_complete(
   SCIC_SDS_REQUEST_T *this_request
)
{
   return this_request->state_handlers->parent.complete_handler(&this_request->parent);
}

//****************************************************************************
//* SCIC SDS PROTECTED METHODS
//****************************************************************************

/**
 * @brief This method copies response data for requests returning response
 *        data instead of sense data.
 *
 * @param[in]  this_request This parameter specifies the request object for
 *             which to copy the response data.
 *
 * @return none
 */
void scic_sds_io_request_copy_response(
   SCIC_SDS_REQUEST_T *this_request
)
{
   void                  * response_buffer;
   U32                     user_response_length;
   U32                     core_response_length;
   SCI_SSP_RESPONSE_IU_T * ssp_response;

   ssp_response = (SCI_SSP_RESPONSE_IU_T *)this_request->response_buffer;

   response_buffer = scic_cb_ssp_task_request_get_response_data_address(
                        this_request->user_request
                     );

   user_response_length = scic_cb_ssp_task_request_get_response_data_length(
                        this_request->user_request
                     );

   core_response_length = sci_ssp_get_response_data_length(
                           ssp_response->response_data_length
                     );

   user_response_length = MIN(user_response_length, core_response_length);

   memcpy(response_buffer, ssp_response->data, user_response_length);
}

//******************************************************************************
//* REQUEST STATE MACHINE
//******************************************************************************

//*****************************************************************************
//*  DEFAULT STATE HANDLERS
//*****************************************************************************

/**
 * This method is the default action to take when an SCIC_SDS_IO_REQUEST_T
 * object receives a scic_sds_request_start() request.  The default action is
 * to log a warning and return a failure status.
 *
 * @param[in] request This is the SCI_BASE_REQUEST_T object that is cast to
 *       the SCIC_SDS_IO_REQUEST_T object for which the start operation is
 *       requested.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_INVALID_STATE
 */
SCI_STATUS scic_sds_request_default_start_handler(
   SCI_BASE_REQUEST_T *request
)
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger((SCIC_SDS_REQUEST_T *)request),
      (
          SCIC_LOG_OBJECT_SSP_IO_REQUEST
        | SCIC_LOG_OBJECT_STP_IO_REQUEST
        | SCIC_LOG_OBJECT_SMP_IO_REQUEST
      ),
      "SCIC IO Request requested to start while in wrong state %d\n",
      sci_base_state_machine_get_state(
         &((SCIC_SDS_REQUEST_T *)request)->parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * This method is the default action to take when an SCIC_SDS_IO_REQUEST_T
 * object receives a scic_sds_request_terminate() request.  The default action
 * is to log a warning and return a failure status.
 *
 * @param[in] request This is the SCI_BASE_REQUEST_T object that is cast to
 *       the SCIC_SDS_IO_REQUEST_T object for which the start operation is
 *       requested.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_INVALID_STATE
 */
SCI_STATUS scic_sds_request_default_abort_handler(
   SCI_BASE_REQUEST_T *request
)
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger((SCIC_SDS_REQUEST_T *)request),
      (
          SCIC_LOG_OBJECT_SSP_IO_REQUEST
        | SCIC_LOG_OBJECT_STP_IO_REQUEST
        | SCIC_LOG_OBJECT_SMP_IO_REQUEST
      ),
      "SCIC IO Request requested to abort while in wrong state %d\n",
      sci_base_state_machine_get_state(
         &((SCIC_SDS_REQUEST_T *)request)->parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * This method is the default action to take when an SCIC_SDS_IO_REQUEST_T
 * object receives a scic_sds_request_complete() request.  The default action
 * is to log a warning and return a failure status.
 *
 * @param[in] request This is the SCI_BASE_REQUEST_T object that is cast to
 *       the SCIC_SDS_IO_REQUEST_T object for which the start operation is
 *       requested.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_INVALID_STATE
 */
SCI_STATUS scic_sds_request_default_complete_handler(
   SCI_BASE_REQUEST_T *request
)
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger((SCIC_SDS_REQUEST_T *)request),
      (
          SCIC_LOG_OBJECT_SSP_IO_REQUEST
        | SCIC_LOG_OBJECT_STP_IO_REQUEST
        | SCIC_LOG_OBJECT_SMP_IO_REQUEST
      ),
      "SCIC IO Request requested to complete while in wrong state %d\n",
      sci_base_state_machine_get_state(
         &((SCIC_SDS_REQUEST_T *)request)->parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * This method is the default action to take when an SCIC_SDS_IO_REQUEST_T
 * object receives a scic_sds_request_complete() request.  The default action
 * is to log a warning and return a failure status.
 *
 * @param[in] request This is the SCI_BASE_REQUEST_T object that is cast to
 *       the SCIC_SDS_IO_REQUEST_T object for which the start operation is
 *       requested.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_INVALID_STATE
 */
SCI_STATUS scic_sds_request_default_destruct_handler(
   SCI_BASE_REQUEST_T *request
)
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger((SCIC_SDS_REQUEST_T *)request),
      (
          SCIC_LOG_OBJECT_SSP_IO_REQUEST
        | SCIC_LOG_OBJECT_STP_IO_REQUEST
        | SCIC_LOG_OBJECT_SMP_IO_REQUEST
      ),
      "SCIC IO Request requested to destroy while in wrong state %d\n",
      sci_base_state_machine_get_state(
         &((SCIC_SDS_REQUEST_T *)request)->parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * This method is the default action to take when an SCIC_SDS_IO_REQUEST_T
 * object receives a scic_sds_task_request_complete() request.  The default
 * action is to log a warning and return a failure status.
 *
 * @param[in] request This is the SCI_BASE_REQUEST_T object that is cast to
 *       the SCIC_SDS_IO_REQUEST_T object for which the start operation is
 *       requested.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_INVALID_STATE
 */
SCI_STATUS scic_sds_request_default_tc_completion_handler(
   SCIC_SDS_REQUEST_T * this_request,
   U32                  completion_code
)
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger(this_request),
      (
          SCIC_LOG_OBJECT_SSP_IO_REQUEST
        | SCIC_LOG_OBJECT_STP_IO_REQUEST
        | SCIC_LOG_OBJECT_SMP_IO_REQUEST
      ),
      "SCIC IO Request given task completion notification %x while in wrong state %d\n",
      completion_code,
      sci_base_state_machine_get_state(&this_request->parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;

}

/**
 * This method is the default action to take when an SCIC_SDS_IO_REQUEST_T
 * object receives a scic_sds_request_event_handler() request.  The default
 * action is to log a warning and return a failure status.
 *
 * @param[in] request This is the SCI_BASE_REQUEST_T object that is cast to
 *       the SCIC_SDS_IO_REQUEST_T object for which the start operation is
 *       requested.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_INVALID_STATE
 */
SCI_STATUS scic_sds_request_default_event_handler(
   SCIC_SDS_REQUEST_T * this_request,
   U32                  event_code
)
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger(this_request),
      (
          SCIC_LOG_OBJECT_SSP_IO_REQUEST
        | SCIC_LOG_OBJECT_STP_IO_REQUEST
        | SCIC_LOG_OBJECT_SMP_IO_REQUEST
      ),
      "SCIC IO Request given event code notification %x while in wrong state %d\n",
      event_code,
      sci_base_state_machine_get_state(&this_request->parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * This method is the default action to take when an SCIC_SDS_IO_REQUEST_T
 * object receives a scic_sds_request_event_handler() request.  The default
 * action is to log a warning and return a failure status.
 *
 * @param[in] request This is the SCI_BASE_REQUEST_T object that is cast to
 *       the SCIC_SDS_IO_REQUEST_T object for which the start operation is
 *       requested.
 *
 * @return SCI_STATUS
 * @retval SCI_FAILURE_INVALID_STATE
 */
SCI_STATUS scic_sds_request_default_frame_handler(
   SCIC_SDS_REQUEST_T * this_request,
   U32                  frame_index
)
{
   SCIC_LOG_WARNING((
      sci_base_object_get_logger(this_request),
      (
          SCIC_LOG_OBJECT_SSP_IO_REQUEST
        | SCIC_LOG_OBJECT_STP_IO_REQUEST
        | SCIC_LOG_OBJECT_SMP_IO_REQUEST
      ),
      "SCIC IO Request given unexpected frame %x while in state %d\n",
      frame_index,
      sci_base_state_machine_get_state(&this_request->parent.state_machine)
   ));

   scic_sds_controller_release_frame(
      this_request->owning_controller, frame_index);

   return SCI_FAILURE_INVALID_STATE;
}

//*****************************************************************************
//*  CONSTRUCTED STATE HANDLERS
//*****************************************************************************

/**
 * This method implements the action taken when a constructed
 * SCIC_SDS_IO_REQUEST_T object receives a scic_sds_request_start() request.
 *
 * This method will, if necessary, allocate a TCi for the io request object
 * and then will, if necessary, copy the constructed TC data into the actual
 * TC buffer.  If everything is successful the post context field is updated
 * with the TCi so the controller can post the request to the hardware.
 *
 * @param[in] request This is the SCI_BASE_REQUEST_T object that is cast to
 *       the SCIC_SDS_IO_REQUEST_T object for which the start operation is
 *       requested.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS
 * @retval SCI_FAILURE_INSUFFICIENT_RESOURCES
 */
static
SCI_STATUS scic_sds_request_constructed_state_start_handler(
   SCI_BASE_REQUEST_T *request
)
{
   SCU_TASK_CONTEXT_T *task_context;
   SCIC_SDS_REQUEST_T *this_request = (SCIC_SDS_REQUEST_T *)request;

   if (this_request->io_tag == SCI_CONTROLLER_INVALID_IO_TAG)
   {
      this_request->io_tag =
         scic_controller_allocate_io_tag(this_request->owning_controller);
   }

   // Record the IO Tag in the request
   if (this_request->io_tag != SCI_CONTROLLER_INVALID_IO_TAG)
   {
      task_context = this_request->task_context_buffer;

      task_context->task_index = scic_sds_io_tag_get_index(this_request->io_tag);

      switch (task_context->protocol_type)
      {
      case SCU_TASK_CONTEXT_PROTOCOL_SMP:
      case SCU_TASK_CONTEXT_PROTOCOL_SSP:
         // SSP/SMP Frame
         task_context->type.ssp.tag = this_request->io_tag;
         task_context->type.ssp.target_port_transfer_tag = 0xFFFF;
         break;

      case SCU_TASK_CONTEXT_PROTOCOL_STP:
         // STP/SATA Frame
         //task_context->type.stp.ncq_tag = this_request->ncq_tag;
         break;

      case SCU_TASK_CONTEXT_PROTOCOL_NONE:
         /// @todo When do we set no protocol type?
         break;

      default:
         // This should never happen since we build the IO requests
         break;
      }

      // Check to see if we need to copy the task context buffer
      // or have been building into the task context buffer
      if (this_request->was_tag_assigned_by_user == FALSE)
      {
         scic_sds_controller_copy_task_context(
            this_request->owning_controller, this_request
         );
      }

      // Add to the post_context the io tag value
      this_request->post_context |= scic_sds_io_tag_get_index(this_request->io_tag);

      // Everything is good go ahead and change state
      sci_base_state_machine_change_state(
         &this_request->parent.state_machine,
         SCI_BASE_REQUEST_STATE_STARTED
      );

      return SCI_SUCCESS;
   }

   return SCI_FAILURE_INSUFFICIENT_RESOURCES;
}

/**
 * This method implements the action to be taken when an SCIC_SDS_IO_REQUEST_T
 * object receives a scic_sds_request_terminate() request.
 *
 * Since the request has not yet been posted to the hardware the request
 * transitions to the completed state.
 *
 * @param[in] request This is the SCI_BASE_REQUEST_T object that is cast to
 *       the SCIC_SDS_IO_REQUEST_T object for which the start operation is
 *       requested.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS
 */
static
SCI_STATUS scic_sds_request_constructed_state_abort_handler(
   SCI_BASE_REQUEST_T *request
)
{
   SCIC_SDS_REQUEST_T *this_request = (SCIC_SDS_REQUEST_T *)request;

   // This request has been terminated by the user make sure that the correct
   // status code is returned
   scic_sds_request_set_status(
      this_request,
      SCU_TASK_DONE_TASK_ABORT,
      SCI_FAILURE_IO_TERMINATED
   );

   sci_base_state_machine_change_state(
      &this_request->parent.state_machine,
      SCI_BASE_REQUEST_STATE_COMPLETED
   );

   return SCI_SUCCESS;
}

//*****************************************************************************
//*  STARTED STATE HANDLERS
//*****************************************************************************

/**
 * This method implements the action to be taken when an SCIC_SDS_IO_REQUEST_T
 * object receives a scic_sds_request_terminate() request.
 *
 * Since the request has been posted to the hardware the io request state is
 * changed to the aborting state.
 *
 * @param[in] request This is the SCI_BASE_REQUEST_T object that is cast to
 *       the SCIC_SDS_IO_REQUEST_T object for which the start operation is
 *       requested.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS
 */
SCI_STATUS scic_sds_request_started_state_abort_handler(
   SCI_BASE_REQUEST_T *request
)
{
   SCIC_SDS_REQUEST_T *this_request = (SCIC_SDS_REQUEST_T *)request;

   if (this_request->has_started_substate_machine)
   {
      sci_base_state_machine_stop(&this_request->started_substate_machine);
   }

   sci_base_state_machine_change_state(
      &this_request->parent.state_machine,
      SCI_BASE_REQUEST_STATE_ABORTING
   );

   return SCI_SUCCESS;
}

/**
 * @brief This method process TC (task context) completions for normal IO
 *        request (i.e. Task/Abort Completions of type 0).  This method will
 *        update the SCIC_SDS_IO_REQUEST_T::status field.
 *
 * @param[in] this_request This parameter specifies the request for which
 *             a completion occurred.
 * @param[in]  completion_code This parameter specifies the completion code
 *             received from the SCU.
 *
 * @return none
 */
SCI_STATUS scic_sds_request_started_state_tc_completion_handler(
   SCIC_SDS_REQUEST_T * this_request,
   U32                  completion_code
)
{
   U8                      data_present;
   SCI_SSP_RESPONSE_IU_T * response_buffer;

   /**
    * @todo Any SDMA return code of other than 0 is bad
    *       decode 0x003C0000 to determine SDMA status
    */
   switch (SCU_GET_COMPLETION_TL_STATUS(completion_code))
   {
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_GOOD):
      scic_sds_request_set_status(
         this_request, SCU_TASK_DONE_GOOD, SCI_SUCCESS
      );
      break;

   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_EARLY_RESP):
   {
      // There are times when the SCU hardware will return an early response
      // because the io request specified more data than is returned by the
      // target device (mode pages, inquiry data, etc.).  We must check the
      // response stats to see if this is truly a failed request or a good
      // request that just got completed early.
      SCI_SSP_RESPONSE_IU_T *response = (SCI_SSP_RESPONSE_IU_T *)
                                        this_request->response_buffer;
      scic_word_copy_with_swap(
         this_request->response_buffer,
         this_request->response_buffer,
         sizeof(SCI_SSP_RESPONSE_IU_T) / sizeof(U32)
      );

      if (response->status == 0)
      {
         scic_sds_request_set_status(
            this_request, SCU_TASK_DONE_GOOD, SCI_SUCCESS_IO_DONE_EARLY
         );
      }
      else
      {
         scic_sds_request_set_status(
            this_request,
            SCU_TASK_DONE_CHECK_RESPONSE,
            SCI_FAILURE_IO_RESPONSE_VALID
         );
      }
   }
   break;

   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_CHECK_RESPONSE):
      scic_word_copy_with_swap(
         this_request->response_buffer,
         this_request->response_buffer,
         sizeof(SCI_SSP_RESPONSE_IU_T) / sizeof(U32)
      );

      scic_sds_request_set_status(
         this_request,
         SCU_TASK_DONE_CHECK_RESPONSE,
         SCI_FAILURE_IO_RESPONSE_VALID
      );
      break;

   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_RESP_LEN_ERR):
      /// @todo With TASK_DONE_RESP_LEN_ERR is the response frame guaranteed
      ///       to be received before this completion status is posted?
      response_buffer =
         (SCI_SSP_RESPONSE_IU_T *)this_request->response_buffer;
      data_present =
         response_buffer->data_present & SCI_SSP_RESPONSE_IU_DATA_PRESENT_MASK;

      if ((data_present == 0x01) || (data_present == 0x02))
      {
         scic_sds_request_set_status(
            this_request,
            SCU_TASK_DONE_CHECK_RESPONSE,
            SCI_FAILURE_IO_RESPONSE_VALID
         );
      }
      else
      {
         scic_sds_request_set_status(
            this_request, SCU_TASK_DONE_GOOD, SCI_SUCCESS
         );
      }
      break;

   //only stp device gets suspended.
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_ACK_NAK_TO):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_LL_PERR):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_NAK_ERR):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_DATA_LEN_ERR):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_LL_ABORT_ERR):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_XR_WD_LEN):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_MAX_PLD_ERR):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_UNEXP_RESP):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_UNEXP_SDBFIS):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_REG_ERR):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_SDB_ERR):
      if (this_request->protocol == SCIC_STP_PROTOCOL)
      {
         SCIC_LOG_ERROR((
            sci_base_object_get_logger(this_request),
            SCIC_LOG_OBJECT_STP_IO_REQUEST,
            "SCIC IO Request 0x%x returning REMOTE_DEVICE_RESET_REQUIRED for completion code 0x%x\n",
            this_request, completion_code
         ));
         scic_sds_request_set_status(
            this_request,
            SCU_GET_COMPLETION_TL_STATUS(completion_code) >> SCU_COMPLETION_TL_STATUS_SHIFT,
            SCI_FAILURE_REMOTE_DEVICE_RESET_REQUIRED
         );
      }
      else
      {
         SCIC_LOG_ERROR((
            sci_base_object_get_logger(this_request),
            SCIC_LOG_OBJECT_SSP_IO_REQUEST,
            "SCIC IO Request 0x%x returning CONTROLLER_SPECIFIC_IO_ERR for completion code 0x%x\n",
            this_request, completion_code
         ));
         scic_sds_request_set_status(
            this_request,
            SCU_GET_COMPLETION_TL_STATUS(completion_code) >> SCU_COMPLETION_TL_STATUS_SHIFT,
            SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR
         );
      }
      break;

   //both stp/ssp device gets suspended
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_LF_ERR):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_OPEN_REJECT_WRONG_DESTINATION):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_OPEN_REJECT_RESERVED_ABANDON_1):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_OPEN_REJECT_RESERVED_ABANDON_2):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_OPEN_REJECT_RESERVED_ABANDON_3):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_OPEN_REJECT_BAD_DESTINATION):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_OPEN_REJECT_ZONE_VIOLATION):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_OPEN_REJECT_STP_RESOURCES_BUSY):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_OPEN_REJECT_PROTOCOL_NOT_SUPPORTED):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_OPEN_REJECT_CONNECTION_RATE_NOT_SUPPORTED):
      scic_sds_request_set_status(
         this_request,
         SCU_GET_COMPLETION_TL_STATUS(completion_code) >> SCU_COMPLETION_TL_STATUS_SHIFT,
         SCI_FAILURE_REMOTE_DEVICE_RESET_REQUIRED
      );
     break;

   //neither ssp nor stp gets suspended.
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_NAK_CMD_ERR):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_UNEXP_XR):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_XR_IU_LEN_ERR):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_SDMA_ERR):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_OFFSET_ERR):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_EXCESS_DATA):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_SMP_RESP_TO_ERR):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_SMP_UFI_ERR):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_SMP_FRM_TYPE_ERR):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_SMP_LL_RX_ERR):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_UNEXP_DATA):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_OPEN_FAIL):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_VIIT_ENTRY_NV):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_IIT_ENTRY_NV):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_RNCNV_OUTBOUND):
   default:
      SCIC_LOG_ERROR((
         sci_base_object_get_logger(this_request),
         SCIC_LOG_OBJECT_SSP_IO_REQUEST | SCIC_LOG_OBJECT_STP_IO_REQUEST,
         "SCIC IO Request 0x%x returning CONTROLLER_SPECIFIC_IO_ERR for completion code 0x%x\n",
         this_request, completion_code
      ));
      scic_sds_request_set_status(
         this_request,
         SCU_GET_COMPLETION_TL_STATUS(completion_code) >> SCU_COMPLETION_TL_STATUS_SHIFT,
         SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR
      );
      break;
   }

   /**
    * @todo This is probably wrong for ACK/NAK timeout conditions
    */

   // In all cases we will treat this as the completion of the IO request.
   sci_base_state_machine_change_state(
      &this_request->parent.state_machine,
      SCI_BASE_REQUEST_STATE_COMPLETED
   );

   return SCI_SUCCESS;
}

/**
 * This method implements the action to be taken when an SCIC_SDS_IO_REQUEST_T
 * object receives a scic_sds_request_frame_handler() request.
 *
 * This method first determines the frame type received.  If this is a
 * response frame then the response data is copied to the io request response
 * buffer for processing at completion time.
 *
 * If the frame type is not a response buffer an error is logged.
 *
 * @param[in] request This is the SCI_BASE_REQUEST_T object that is cast to
 *       the SCIC_SDS_IO_REQUEST_T object for which the start operation is
 *       requested.
 * @param[in] frame_index This is the index of the unsolicited frame to be
 *       processed.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS
 * @retval SCI_FAILURE_INVALID_PARAMETER_VALUE
 */
static
SCI_STATUS scic_sds_request_started_state_frame_handler(
   SCIC_SDS_REQUEST_T * this_request,
   U32                  frame_index
)
{
   SCI_STATUS status;
   SCI_SSP_FRAME_HEADER_T *frame_header;

   /// @todo If this is a response frame we must record that we received it
   status = scic_sds_unsolicited_frame_control_get_header(
      &(scic_sds_request_get_controller(this_request)->uf_control),
      frame_index,
      (void**) &frame_header
   );

   if (frame_header->frame_type == SCI_SAS_RESPONSE_FRAME)
   {
      SCI_SSP_RESPONSE_IU_T *response_buffer;

      status = scic_sds_unsolicited_frame_control_get_buffer(
         &(scic_sds_request_get_controller(this_request)->uf_control),
         frame_index,
         (void**) &response_buffer
      );

      scic_word_copy_with_swap(
         this_request->response_buffer,
         (U32 *)response_buffer,
         sizeof(SCI_SSP_RESPONSE_IU_T)
      );

      response_buffer = (SCI_SSP_RESPONSE_IU_T *)this_request->response_buffer;

      if (
            (response_buffer->data_present == 0x01)
         || (response_buffer->data_present == 0x02)
         )
      {
         scic_sds_request_set_status(
            this_request,
            SCU_TASK_DONE_CHECK_RESPONSE,
            SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR
         );
      }
      else
      {
         scic_sds_request_set_status(
            this_request, SCU_TASK_DONE_GOOD, SCI_SUCCESS
         );
      }

   }
   else
   {
      // This was not a response frame why did it get forwarded?
      SCIC_LOG_ERROR((
         sci_base_object_get_logger(this_request),
         SCIC_LOG_OBJECT_SSP_IO_REQUEST,
         "SCIC IO Request 0x%x received unexpected frame %d type 0x%02x\n",
         this_request, frame_index, frame_header->frame_type
      ));
   }

   // In any case we are done with this frame buffer return it to the
   // controller
   scic_sds_controller_release_frame(
      this_request->owning_controller, frame_index
   );

   return SCI_SUCCESS;
}

//*****************************************************************************
//*  COMPLETED STATE HANDLERS
//*****************************************************************************


/**
 * This method implements the action to be taken when an SCIC_SDS_IO_REQUEST_T
 * object receives a scic_sds_request_complete() request.
 *
 * This method frees up any io request resources that have been allocated and
 * transitions the request to its final state.
 *
 * @todo Consider stopping the state machine instead of transitioning to the
 *       final state?
 *
 * @param[in] request This is the SCI_BASE_REQUEST_T object that is cast to
 *       the SCIC_SDS_IO_REQUEST_T object for which the start operation is
 *       requested.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS
 */
static
SCI_STATUS scic_sds_request_completed_state_complete_handler(
   SCI_BASE_REQUEST_T *request
)
{
   SCIC_SDS_REQUEST_T *this_request = (SCIC_SDS_REQUEST_T *)request;

   if (this_request->was_tag_assigned_by_user != TRUE)
   {
      scic_controller_free_io_tag(
         this_request->owning_controller, this_request->io_tag
      );
   }

   if (this_request->saved_rx_frame_index != SCU_INVALID_FRAME_INDEX)
   {
      scic_sds_controller_release_frame(
         this_request->owning_controller, this_request->saved_rx_frame_index);
   }

   sci_base_state_machine_change_state(
      &this_request->parent.state_machine,
      SCI_BASE_REQUEST_STATE_FINAL
   );

   scic_sds_request_deinitialize_state_logging(this_request);

   return SCI_SUCCESS;
}

//*****************************************************************************
//*  ABORTING STATE HANDLERS
//*****************************************************************************

/**
 * This method implements the action to be taken when an SCIC_SDS_IO_REQUEST_T
 * object receives a scic_sds_request_terminate() request.
 *
 * This method is the io request aborting state abort handlers.  On receipt of
 * a multiple terminate requests the io request will transition to the
 * completed state.  This should not happen in normal operation.
 *
 * @param[in] request This is the SCI_BASE_REQUEST_T object that is cast to
 *       the SCIC_SDS_IO_REQUEST_T object for which the start operation is
 *       requested.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS
 */
static
SCI_STATUS scic_sds_request_aborting_state_abort_handler(
   SCI_BASE_REQUEST_T *request
)
{
   SCIC_SDS_REQUEST_T *this_request = (SCIC_SDS_REQUEST_T *)request;

   sci_base_state_machine_change_state(
      &this_request->parent.state_machine,
      SCI_BASE_REQUEST_STATE_COMPLETED
   );

   return SCI_SUCCESS;
}

/**
 * This method implements the action to be taken when an SCIC_SDS_IO_REQUEST_T
 * object receives a scic_sds_request_task_completion() request.
 *
 * This method decodes the completion type waiting for the abort task complete
 * notification. When the abort task complete is received the io request
 * transitions to the completed state.
 *
 * @param[in] request This is the SCI_BASE_REQUEST_T object that is cast to
 *       the SCIC_SDS_IO_REQUEST_T object for which the start operation is
 *       requested.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS
 */
static
SCI_STATUS scic_sds_request_aborting_state_tc_completion_handler(
   SCIC_SDS_REQUEST_T * this_request,
   U32                  completion_code
)
{
   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_request),
      SCIC_LOG_OBJECT_TASK_MANAGEMENT,
      "scic_sds_request_aborting_state_tc_completion_handler(0x%x,0x%x) enter\n",
      this_request, completion_code
   ));

   switch (SCU_GET_COMPLETION_TL_STATUS(completion_code))
   {
   case (SCU_TASK_DONE_GOOD << SCU_COMPLETION_TL_STATUS_SHIFT):
   case (SCU_TASK_DONE_TASK_ABORT << SCU_COMPLETION_TL_STATUS_SHIFT):
      scic_sds_request_set_status(
         this_request, SCU_TASK_DONE_TASK_ABORT, SCI_FAILURE_IO_TERMINATED
      );

      sci_base_state_machine_change_state(
         &this_request->parent.state_machine,
         SCI_BASE_REQUEST_STATE_COMPLETED
      );
      break;

   default:
      // Unless we get some strange error wait for the task abort to complete
      // TODO: Should there be a state change for this completion?
      break;
   }

   return SCI_SUCCESS;
}

/**
 * This method implements the action to be taken when an SCIC_SDS_IO_REQUEST_T
 * object receives a scic_sds_request_frame_handler() request.
 *
 * This method discards the unsolicited frame since we are waiting for the
 * abort task completion.
 *
 * @param[in] request This is the SCI_BASE_REQUEST_T object that is cast to
 *       the SCIC_SDS_IO_REQUEST_T object for which the start operation is
 *       requested.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS
 */
static
SCI_STATUS scic_sds_request_aborting_state_frame_handler(
   SCIC_SDS_REQUEST_T * this_request,
   U32                  frame_index
)
{
   // TODO: Is it even possible to get an unsolicited frame in the aborting state?

   scic_sds_controller_release_frame(
      this_request->owning_controller, frame_index);

   return SCI_SUCCESS;
}

// ---------------------------------------------------------------------------

SCIC_SDS_IO_REQUEST_STATE_HANDLER_T
   scic_sds_request_state_handler_table[SCI_BASE_REQUEST_MAX_STATES] =
{
   // SCI_BASE_REQUEST_STATE_INITIAL
   {
      {
         scic_sds_request_default_start_handler,
         scic_sds_request_default_abort_handler,
         scic_sds_request_default_complete_handler,
         scic_sds_request_default_destruct_handler
      },
      scic_sds_request_default_tc_completion_handler,
      scic_sds_request_default_event_handler,
      scic_sds_request_default_frame_handler
   },
   // SCI_BASE_REQUEST_STATE_CONSTRUCTED
   {
      {
         scic_sds_request_constructed_state_start_handler,
         scic_sds_request_constructed_state_abort_handler,
         scic_sds_request_default_complete_handler,
         scic_sds_request_default_destruct_handler
      },
      scic_sds_request_default_tc_completion_handler,
      scic_sds_request_default_event_handler,
      scic_sds_request_default_frame_handler
   },
   // SCI_BASE_REQUEST_STATE_STARTED
   {
      {
         scic_sds_request_default_start_handler,
         scic_sds_request_started_state_abort_handler,
         scic_sds_request_default_complete_handler,
         scic_sds_request_default_destruct_handler
      },
      scic_sds_request_started_state_tc_completion_handler,
      scic_sds_request_default_event_handler,
      scic_sds_request_started_state_frame_handler
   },
   // SCI_BASE_REQUEST_STATE_COMPLETED
   {
      {
         scic_sds_request_default_start_handler,
         scic_sds_request_default_abort_handler,
         scic_sds_request_completed_state_complete_handler,
         scic_sds_request_default_destruct_handler
      },
      scic_sds_request_default_tc_completion_handler,
      scic_sds_request_default_event_handler,
      scic_sds_request_default_frame_handler
   },
   // SCI_BASE_REQUEST_STATE_ABORTING
   {
      {
         scic_sds_request_default_start_handler,
         scic_sds_request_aborting_state_abort_handler,
         scic_sds_request_default_complete_handler,
         scic_sds_request_default_destruct_handler
      },
      scic_sds_request_aborting_state_tc_completion_handler,
      scic_sds_request_default_event_handler,
      scic_sds_request_aborting_state_frame_handler,
   },
   // SCI_BASE_REQUEST_STATE_FINAL
   {
      {
         scic_sds_request_default_start_handler,
         scic_sds_request_default_abort_handler,
         scic_sds_request_default_complete_handler,
         scic_sds_request_default_destruct_handler
      },
      scic_sds_request_default_tc_completion_handler,
      scic_sds_request_default_event_handler,
      scic_sds_request_default_frame_handler
   }
};

/**
 * This method implements the actions taken when entering the
 * SCI_BASE_REQUEST_STATE_INITIAL state. This state is entered when the
 * initial base request is constructed. Entry into the initial state sets all
 * handlers for the io request object to their default handlers.
 *
 * @param[in] object This parameter specifies the base object for which the
 *       state transition is occurring.
 *
 * @return none
 */
static
void scic_sds_request_initial_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REQUEST_T *this_request = (SCIC_SDS_REQUEST_T *)object;

   SET_STATE_HANDLER(
      this_request,
      scic_sds_request_state_handler_table,
      SCI_BASE_REQUEST_STATE_INITIAL
   );
}

/**
 * This method implements the actions taken when entering the
 * SCI_BASE_REQUEST_STATE_CONSTRUCTED state.
 * The method sets the state handlers for the constructed state.
 *
 * @param[in] object The io request object that is to enter the constructed
 *       state.
 *
 * @return none
 */
static
void scic_sds_request_constructed_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REQUEST_T *this_request = (SCIC_SDS_REQUEST_T *)object;

   SET_STATE_HANDLER(
      this_request,
      scic_sds_request_state_handler_table,
      SCI_BASE_REQUEST_STATE_CONSTRUCTED
   );
}

/**
 * This method implements the actions taken when entering the
 * SCI_BASE_REQUEST_STATE_STARTED state. If the io request object type is a
 * SCSI Task request we must enter the started substate machine.
 *
 * @param[in] object This parameter specifies the base object for which the
 *       state transition is occurring.  This is cast into a
 *       SCIC_SDS_IO_REQUEST object.
 *
 * @return none
 */
static
void scic_sds_request_started_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REQUEST_T *this_request = (SCIC_SDS_REQUEST_T *)object;

   SET_STATE_HANDLER(
      this_request,
      scic_sds_request_state_handler_table,
      SCI_BASE_REQUEST_STATE_STARTED
   );

   // Most of the request state machines have a started substate machine so
   // start its execution on the entry to the started state.
   if (this_request->has_started_substate_machine == TRUE)
      sci_base_state_machine_start(&this_request->started_substate_machine);
}

/**
 * This method implements the actions taken when exiting the
 * SCI_BASE_REQUEST_STATE_STARTED state. For task requests the action will be
 * to stop the started substate machine.
 *
 * @param[in] object This parameter specifies the base object for which the
 *       state transition is occurring.  This object is cast into a
 *       SCIC_SDS_IO_REQUEST object.
 *
 * @return none
 */
static
void scic_sds_request_started_state_exit(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REQUEST_T *this_request = (SCIC_SDS_REQUEST_T *)object;

   if (this_request->has_started_substate_machine == TRUE)
      sci_base_state_machine_stop(&this_request->started_substate_machine);
}

/**
 * This method implements the actions taken when entering the
 * SCI_BASE_REQUEST_STATE_COMPLETED state.  This state is entered when the
 * SCIC_SDS_IO_REQUEST has completed.  The method will decode the request
 * completion status and convert it to an SCI_STATUS to return in the
 * completion callback function.
 *
 * @param[in] object This parameter specifies the base object for which the
 *       state transition is occurring.  This object is cast into a
 *       SCIC_SDS_IO_REQUEST object.
 *
 * @return none
 */
static
void scic_sds_request_completed_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REQUEST_T *this_request = (SCIC_SDS_REQUEST_T *)object;

   SET_STATE_HANDLER(
      this_request,
      scic_sds_request_state_handler_table,
      SCI_BASE_REQUEST_STATE_COMPLETED
   );

   // Tell the SCI_USER that the IO request is complete
   if (this_request->is_task_management_request == FALSE)
   {
      scic_cb_io_request_complete(
         scic_sds_request_get_controller(this_request),
         scic_sds_request_get_device(this_request),
         this_request,
         this_request->sci_status
      );
   }
   else
   {
      scic_cb_task_request_complete(
         scic_sds_request_get_controller(this_request),
         scic_sds_request_get_device(this_request),
         this_request,
         this_request->sci_status
      );
   }
}

/**
 * This method implements the actions taken when entering the
 * SCI_BASE_REQUEST_STATE_ABORTING state.
 *
 * @param[in] object This parameter specifies the base object for which the
 *       state transition is occurring.  This object is cast into a
 *       SCIC_SDS_IO_REQUEST object.
 *
 * @return none
 */
static
void scic_sds_request_aborting_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REQUEST_T *this_request = (SCIC_SDS_REQUEST_T *)object;

   // Setting the abort bit in the Task Context is required by the silicon.
   this_request->task_context_buffer->abort = 1;

   SET_STATE_HANDLER(
      this_request,
      scic_sds_request_state_handler_table,
      SCI_BASE_REQUEST_STATE_ABORTING
   );
}

/**
 * This method implements the actions taken when entering the
 * SCI_BASE_REQUEST_STATE_FINAL state. The only action required is to put the
 * state handlers in place.
 *
 * @param[in] object This parameter specifies the base object for which the
 *       state transition is occurring.  This is cast into a
 *       SCIC_SDS_IO_REQUEST object.
 *
 * @return none
 */
static
void scic_sds_request_final_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REQUEST_T *this_request = (SCIC_SDS_REQUEST_T *)object;

   SET_STATE_HANDLER(
      this_request,
      scic_sds_request_state_handler_table,
      SCI_BASE_REQUEST_STATE_FINAL
   );
}

// ---------------------------------------------------------------------------

SCI_BASE_STATE_T
   scic_sds_request_state_table[SCI_BASE_REQUEST_MAX_STATES] =
{
   {
      SCI_BASE_REQUEST_STATE_INITIAL,
      scic_sds_request_initial_state_enter,
      NULL
   },
   {
      SCI_BASE_REQUEST_STATE_CONSTRUCTED,
      scic_sds_request_constructed_state_enter,
      NULL
   },
   {
      SCI_BASE_REQUEST_STATE_STARTED,
      scic_sds_request_started_state_enter,
      scic_sds_request_started_state_exit
   },
   {
      SCI_BASE_REQUEST_STATE_COMPLETED,
      scic_sds_request_completed_state_enter,
      NULL
   },
   {
      SCI_BASE_REQUEST_STATE_ABORTING,
      scic_sds_request_aborting_state_enter,
      NULL
   },
   {
      SCI_BASE_REQUEST_STATE_FINAL,
      scic_sds_request_final_state_enter,
      NULL
   }
};

