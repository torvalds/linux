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

#include <dev/isci/scil/intel_sat.h>
#include <dev/isci/scil/intel_sata.h>
#include <dev/isci/scil/sci_types.h>
#include <dev/isci/scil/scic_remote_device.h>
#include <dev/isci/scil/scic_user_callback.h>
#include <dev/isci/scil/scic_sds_controller.h>
#include <dev/isci/scil/scic_sds_remote_device.h>
#include <dev/isci/scil/scic_sds_stp_request.h>
#include <dev/isci/scil/scic_sds_stp_pio_request.h>
#include <dev/isci/scil/scic_sds_logger.h>
#include <dev/isci/sci_environment.h>
#include <dev/isci/scil/sci_base_state_machine.h>
#include <dev/isci/scil/scu_task_context.h>
#include <dev/isci/scil/intel_ata.h>
#include <dev/isci/scil/sci_util.h>
#include <dev/isci/scil/scic_sds_logger.h>
#include <dev/isci/scil/scic_sds_request.h>
#include <dev/isci/scil/scic_sds_stp_request.h>
#include <dev/isci/scil/scu_completion_codes.h>
#include <dev/isci/scil/scu_event_codes.h>
#include <dev/isci/scil/sci_base_state.h>
#include <dev/isci/scil/scic_sds_unsolicited_frame_control.h>
#include <dev/isci/scil/scic_io_request.h>

#if !defined(DISABLE_ATAPI)
#include <dev/isci/scil/scic_sds_stp_packet_request.h>
#endif

/**
 * This macro returns the address of the stp h2d reg fis buffer in the io
 * request memory
 */
#define scic_sds_stp_request_get_h2d_reg_buffer_unaligned(memory) \
   ((SATA_FIS_REG_H2D_T *)( \
      ((char *)(memory)) + sizeof(SCIC_SDS_STP_REQUEST_T) \
   ))

/**
 * This macro aligns the stp command buffer in DWORD alignment
*/
#define scic_sds_stp_request_align_h2d_reg_buffer(address) \
   ((SATA_FIS_REG_H2D_T *)( \
      (((POINTER_UINT)(address)) + (sizeof(U32) - 1)) \
         & ~(sizeof(U32)- 1) \
      ))

/**
 * This macro returns the DWORD-aligned stp command buffer
*/
#define scic_sds_stp_request_get_h2d_reg_buffer(memory) \
   ((SATA_FIS_REG_H2D_T *)  \
       ((char *)scic_sds_stp_request_align_h2d_reg_buffer( \
       (char *) scic_sds_stp_request_get_h2d_reg_buffer_unaligned(memory) \
   )))

/**
 * This macro returns the address of the stp response buffer in the io
 * request memory
 */
#define scic_sds_stp_request_get_response_buffer_unaligned(memory) \
   ((SATA_FIS_REG_D2H_T *)( \
         ((char *)(scic_sds_stp_request_get_h2d_reg_buffer(memory))) \
       + sizeof(SATA_FIS_REG_H2D_T) \
   ))


/**
 * This macro aligns the stp response buffer in DWORD alignment
*/
#define scic_sds_stp_request_align_response_buffer(address) \
   ((SATA_FIS_REG_D2H_T *)( \
      (((POINTER_UINT)(address)) + (sizeof(U32) - 1)) \
         & ~(sizeof(U32)- 1) \
   ))

/**
 * This macro returns the DWORD-aligned stp response buffer
*/
#define scic_sds_stp_request_get_response_buffer(memory) \
   ((SATA_FIS_REG_D2H_T *)  \
      ((char *)scic_sds_stp_request_align_response_buffer( \
         (char *)scic_sds_stp_request_get_response_buffer_unaligned(memory) \
   )))


/**
 * This macro returns the address of the task context buffer in the io
 * request memory
 */
#define scic_sds_stp_request_get_task_context_buffer_unaligned(memory) \
   ((SCU_TASK_CONTEXT_T *)( \
        ((char *)(scic_sds_stp_request_get_response_buffer(memory))) \
      + sizeof(SCI_SSP_RESPONSE_IU_T) \
   ))

/**
 * This macro returns the aligned task context buffer
 */
#define scic_sds_stp_request_get_task_context_buffer(memory) \
   ((SCU_TASK_CONTEXT_T *)( \
      ((char *)scic_sds_request_align_task_context_buffer( \
         (char *)scic_sds_stp_request_get_task_context_buffer_unaligned(memory)) \
    )))

/**
 * This macro returns the address of the sgl elment pairs in the io request
 * memory buffer
 */
#define scic_sds_stp_request_get_sgl_element_buffer(memory) \
   ((SCU_SGL_ELEMENT_PAIR_T *)( \
        ((char *)(scic_sds_stp_request_get_task_context_buffer(memory))) \
      + sizeof(SCU_TASK_CONTEXT_T) \
    ))


/**
 * This method return the memory space commonly required for STP IO and
 * task requests.
 *
 * @return U32
 */
static
U32 scic_sds_stp_common_request_get_object_size(void)
{
   return   sizeof(SCIC_SDS_STP_REQUEST_T)
          + sizeof(SATA_FIS_REG_H2D_T)
          + sizeof(U32)
          + sizeof(SATA_FIS_REG_D2H_T)
          + sizeof(U32)
          + sizeof(SCU_TASK_CONTEXT_T)
          + CACHE_LINE_SIZE;
}


/**
 * This method return the memory space required for STP PIO requests.
 *
 * @return U32
 */
U32 scic_sds_stp_request_get_object_size(void)
{
   return   scic_sds_stp_common_request_get_object_size()
          + sizeof(SCU_SGL_ELEMENT_PAIR_T) * SCU_MAX_SGL_ELEMENT_PAIRS;
}


/**
 * This method return the memory space required for STP task requests.
 *
 * @return U32
 */
U32 scic_sds_stp_task_request_get_object_size(void)
{
   return scic_sds_stp_common_request_get_object_size();
}


/**
 *
 *
 * @param[in] this_request
 */
void scic_sds_stp_request_assign_buffers(
   SCIC_SDS_REQUEST_T * request
)
{
   SCIC_SDS_STP_REQUEST_T * this_request = (SCIC_SDS_STP_REQUEST_T *)request;

   this_request->parent.command_buffer =
      scic_sds_stp_request_get_h2d_reg_buffer(this_request);
   this_request->parent.response_buffer =
      scic_sds_stp_request_get_response_buffer(this_request);
   this_request->parent.sgl_element_pair_buffer =
      scic_sds_stp_request_get_sgl_element_buffer(this_request);
   this_request->parent.sgl_element_pair_buffer =
      scic_sds_request_align_sgl_element_buffer(this_request->parent.sgl_element_pair_buffer);

   if (this_request->parent.was_tag_assigned_by_user == FALSE)
   {
      this_request->parent.task_context_buffer =
         scic_sds_stp_request_get_task_context_buffer(this_request);
   }
}

/**
 * @brief This method is will fill in the SCU Task Context for any type of
 *        SATA request.  This is called from the various SATA constructors.
 *
 * @pre The general io request construction is complete.
 * @pre The buffer assignment for the command buffer is complete.
 *
 * @param[in] this_request The general IO request object which is to be used
 *       in constructing the SCU task context.
 * @param[in] task_context The buffer pointer for the SCU task context which
 *       is being constructed.
 *
 * @return none
 *
 * @todo Revisit task context construction to determine what is common for
 *       SSP/SMP/STP task context structures.
 */
void scu_sata_reqeust_construct_task_context(
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
   task_context->priority = SCU_TASK_PRIORITY_NORMAL;
   task_context->initiator_request = 1;
   task_context->connection_rate =
      scic_remote_device_get_connection_rate(target_device);
   task_context->protocol_engine_index =
      scic_sds_controller_get_protocol_engine_group(owning_controller);
   task_context->logical_port_index =
      scic_sds_port_get_index(target_port);
   task_context->protocol_type = SCU_TASK_CONTEXT_PROTOCOL_STP;
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
   task_context->task_phase = 0x01;

   task_context->ssp_command_iu_length =
      (sizeof(SATA_FIS_REG_H2D_T) - sizeof(U32)) / sizeof(U32);

   // Set the first word of the H2D REG FIS
   task_context->type.words[0] = *(U32 *)this_request->command_buffer;

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
   // We must offset the command buffer by 4 bytes because the first 4 bytes are
   // transferred in the body of the TC
   scic_cb_io_request_get_physical_address(
      scic_sds_request_get_controller(this_request),
      this_request,
      ((char *)this_request->command_buffer) + sizeof(U32),
      &physical_address
   );

   task_context->command_iu_upper =
      sci_cb_physical_address_upper(physical_address);
   task_context->command_iu_lower =
      sci_cb_physical_address_lower(physical_address);

   // SATA Requests do not have a response buffer
   task_context->response_iu_upper = 0;
   task_context->response_iu_lower = 0;
}

/**
 * This method will perform any general sata request construction.
 *
 * @todo What part of SATA IO request construction is general?
 *
 * @param[in] this_request
 *
 * @return none
 */
void scic_sds_stp_non_ncq_request_construct(
   SCIC_SDS_REQUEST_T * this_request
)
{
   this_request->has_started_substate_machine = TRUE;
}

/**
 * This method will perform request construction common to all types of
 * STP requests that are optimized by the silicon (i.e. UDMA, NCQ).
 *
 * @param[in,out] this_request This parameter specifies the request to be
 *                constructed as an optimized request.
 * @param[in] optimized_task_type This parameter specifies whether the
 *            request is to be an UDMA request or a NCQ request.
 *            - A value of 0 indicates UDMA.
 *            - A value of 1 indicates NCQ.
 *
 * @return This method returns an indication as to whether the construction
 *         was successful.
 */
static
void scic_sds_stp_optimized_request_construct(
   SCIC_SDS_REQUEST_T * this_request,
   U8                   optimized_task_type,
   U32                  transfer_length,
   SCI_IO_REQUEST_DATA_DIRECTION data_direction
)
{
   SCU_TASK_CONTEXT_T * task_context = this_request->task_context_buffer;

   // Build the STP task context structure
   scu_sata_reqeust_construct_task_context(this_request, task_context);

   // Copy over the number of bytes to be transferred
   task_context->transfer_length_bytes = transfer_length;

   if ( data_direction == SCI_IO_REQUEST_DATA_OUT )
   {
      // The difference between the DMA IN and DMA OUT request task type
      // values are consistent with the difference between FPDMA READ
      // and FPDMA WRITE values.  Add the supplied task type parameter
      // to this difference to set the task type properly for this
      // DATA OUT (WRITE) case.
      task_context->task_type = optimized_task_type + (SCU_TASK_TYPE_DMA_OUT
                                                     - SCU_TASK_TYPE_DMA_IN);
   }
   else
   {
      // For the DATA IN (READ) case, simply save the supplied
      // optimized task type.
      task_context->task_type = optimized_task_type;
   }
}

/**
 * This method performs the operations common to all SATA/STP requests
 * utilizing the raw frame method.
 *
 * @param[in] this_request This parameter specifies the STP request object
 *            for which to construct a RAW command frame task context.
 * @param[in] task_context This parameter specifies the SCU specific
 *            task context buffer to construct.
 *
 * @return none
 */
void scu_stp_raw_request_construct_task_context(
   SCIC_SDS_STP_REQUEST_T * this_request,
   SCU_TASK_CONTEXT_T     * task_context
)
{
   scu_sata_reqeust_construct_task_context(&this_request->parent, task_context);

   task_context->control_frame         = 0;
   task_context->priority              = SCU_TASK_PRIORITY_NORMAL;
   task_context->task_type             = SCU_TASK_TYPE_SATA_RAW_FRAME;
   task_context->type.stp.fis_type     = SATA_FIS_TYPE_REGH2D;
   task_context->transfer_length_bytes = sizeof(SATA_FIS_REG_H2D_T) - sizeof(U32);
}

/**
 * This method will construct the STP Non-data request and its associated
 * TC data.  A non-data request essentially behaves like a 0 length read
 * request in the SCU.
 *
 * @param[in] this_request This parameter specifies the core request
 *            object to construction into an STP/SATA non-data request.
 *
 * @return This method currently always returns SCI_SUCCESS
 */
SCI_STATUS scic_sds_stp_non_data_request_construct(
   SCIC_SDS_REQUEST_T * this_request
)
{
   scic_sds_stp_non_ncq_request_construct(this_request);

   // Build the STP task context structure
   scu_stp_raw_request_construct_task_context(
      (SCIC_SDS_STP_REQUEST_T*) this_request,
      this_request->task_context_buffer
   );

   sci_base_state_machine_construct(
      &this_request->started_substate_machine,
      &this_request->parent.parent,
      scic_sds_stp_request_started_non_data_substate_table,
      SCIC_SDS_STP_REQUEST_STARTED_NON_DATA_AWAIT_H2D_COMPLETION_SUBSTATE
   );

   return SCI_SUCCESS;
}


SCI_STATUS scic_sds_stp_soft_reset_request_construct(
   SCIC_SDS_REQUEST_T * this_request
)
{
   scic_sds_stp_non_ncq_request_construct(this_request);

   // Build the STP task context structure
   scu_stp_raw_request_construct_task_context(
      (SCIC_SDS_STP_REQUEST_T*) this_request,
      this_request->task_context_buffer
   );

   sci_base_state_machine_construct(
      &this_request->started_substate_machine,
      &this_request->parent.parent,
      scic_sds_stp_request_started_soft_reset_substate_table,
      SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_H2D_ASSERTED_COMPLETION_SUBSTATE
   );

   return SCI_SUCCESS;
}

/**
 * @brief This method constructs the SATA request object.
 *
 * @param[in] this_request
 * @param[in] sat_protocol
 * @param[in] transfer_length
 * @param[in] data_direction
 * @param[in] copy_rx_frame
 * @param[in] do_translate_sgl This parameter specifies whether SGL
 *            translation should be performed or if the user is handling
 *            it.
 *
 * @return SCI_STATUS
 */
SCI_STATUS scic_sds_io_request_construct_sata(
   SCIC_SDS_REQUEST_T          * this_request,
   U8                            sat_protocol,
   U32                           transfer_length,
   SCI_IO_REQUEST_DATA_DIRECTION data_direction,
   BOOL                          copy_rx_frame,
   BOOL                          do_translate_sgl
)
{
   SCI_STATUS  status = SCI_SUCCESS;

   this_request->protocol = SCIC_STP_PROTOCOL;

   this_request->sat_protocol = sat_protocol;

   switch (sat_protocol)
   {
   case SAT_PROTOCOL_FPDMA:
      scic_sds_stp_optimized_request_construct(
         this_request,
         SCU_TASK_TYPE_FPDMAQ_READ,
         transfer_length,
         data_direction
      );

      // Copy over the SGL elements
      if (do_translate_sgl == TRUE)
         scic_sds_request_build_sgl(this_request);
   break;

   case SAT_PROTOCOL_UDMA_DATA_IN:
   case SAT_PROTOCOL_UDMA_DATA_OUT:
      scic_sds_stp_non_ncq_request_construct(this_request);

      scic_sds_stp_optimized_request_construct(
         this_request, SCU_TASK_TYPE_DMA_IN, transfer_length, data_direction
      );

      // Copy over the SGL elements
      if (do_translate_sgl == TRUE)
         scic_sds_request_build_sgl(this_request);

      sci_base_state_machine_construct(
         &this_request->started_substate_machine,
         &this_request->parent.parent,
         scic_sds_stp_request_started_udma_substate_table,
         SCIC_SDS_STP_REQUEST_STARTED_UDMA_AWAIT_TC_COMPLETION_SUBSTATE
      );
   break;

   case SAT_PROTOCOL_PIO_DATA_IN:
   case SAT_PROTOCOL_PIO_DATA_OUT:
      status = scic_sds_stp_pio_request_construct(
                  this_request, sat_protocol, copy_rx_frame);
   break;

   case SAT_PROTOCOL_ATA_HARD_RESET:
   case SAT_PROTOCOL_SOFT_RESET:
      status = scic_sds_stp_soft_reset_request_construct(this_request);
   break;

   case SAT_PROTOCOL_NON_DATA:
      status = scic_sds_stp_non_data_request_construct(this_request);
   break;

#if !defined(DISABLE_ATAPI)
   case SAT_PROTOCOL_PACKET_NON_DATA:
   case SAT_PROTOCOL_PACKET_DMA_DATA_IN:
   case SAT_PROTOCOL_PACKET_DMA_DATA_OUT:
   case SAT_PROTOCOL_PACKET_PIO_DATA_IN:
   case SAT_PROTOCOL_PACKET_PIO_DATA_OUT:
      status = scic_sds_stp_packet_request_construct(this_request);
      if (do_translate_sgl == TRUE)
         scic_sds_request_build_sgl(this_request);
   break;
#endif

   case SAT_PROTOCOL_DMA_QUEUED:
   case SAT_PROTOCOL_DMA:
   case SAT_PROTOCOL_DEVICE_DIAGNOSTIC:
   case SAT_PROTOCOL_DEVICE_RESET:
   case SAT_PROTOCOL_RETURN_RESPONSE_INFO:
   default:
      SCIC_LOG_ERROR((
         sci_base_object_get_logger(this_request),
         SCIC_LOG_OBJECT_STP_IO_REQUEST,
         "SCIC IO Request 0x%x received un-handled SAT Protocol %d.\n",
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

//****************************************************************************
//* SCIC Interface Implementation
//****************************************************************************

void scic_stp_io_request_set_ncq_tag(
   SCI_IO_REQUEST_HANDLE_T  scic_io_request,
   U16                      ncq_tag
)
{
   /**
    * @note This could be made to return an error to the user if the user
    *       attempts to set the NCQ tag in the wrong state.
    */
   SCIC_SDS_REQUEST_T * this_request = (SCIC_SDS_REQUEST_T *)scic_io_request;
   this_request->task_context_buffer->type.stp.ncq_tag = ncq_tag;
}

// ---------------------------------------------------------------------------

void * scic_stp_io_request_get_h2d_reg_address(
   SCI_IO_REQUEST_HANDLE_T  scic_io_request
)
{
   SCIC_SDS_REQUEST_T * this_request = (SCIC_SDS_REQUEST_T *)scic_io_request;

   return this_request->command_buffer;
}

// ---------------------------------------------------------------------------

void * scic_stp_io_request_get_d2h_reg_address(
   SCI_IO_REQUEST_HANDLE_T  scic_io_request
)
{
   SCIC_SDS_STP_REQUEST_T * this_request = (SCIC_SDS_STP_REQUEST_T *)scic_io_request;

   return &this_request->d2h_reg_fis;
}

/**
 * Get the next SGL element from the request.
 *    - Check on which SGL element pair we are working
 *    - if working on SLG pair element A
 *       - advance to element B
 *    - else
 *       - check to see if there are more SGL element pairs
 *           for this IO request
 *       - if there are more SGL element pairs
 *          - advance to the next pair and return element A
 *
 * @param[in] this_request
 *
 * @return SCU_SGL_ELEMENT_T*
 */
SCU_SGL_ELEMENT_T * scic_sds_stp_request_pio_get_next_sgl(
   SCIC_SDS_STP_REQUEST_T * this_request
)
{
   SCU_SGL_ELEMENT_T * current_sgl;

   if (this_request->type.pio.request_current.sgl_set == SCU_SGL_ELEMENT_PAIR_A)
   {
      if (
            (this_request->type.pio.request_current.sgl_pair->B.address_lower == 0)
         && (this_request->type.pio.request_current.sgl_pair->B.address_upper == 0)
         )
      {
         current_sgl = NULL;
      }
      else
      {
         this_request->type.pio.request_current.sgl_set = SCU_SGL_ELEMENT_PAIR_B;
         current_sgl = &(this_request->type.pio.request_current.sgl_pair->B);
      }
   }
   else
   {
      if (
            (this_request->type.pio.request_current.sgl_pair->next_pair_lower == 0)
         && (this_request->type.pio.request_current.sgl_pair->next_pair_upper == 0)
         )
      {
         current_sgl = NULL;
      }
      else
      {
         this_request->type.pio.request_current.sgl_pair =
            scic_sds_request_get_sgl_element_pair(
               &(this_request->parent),
               ++this_request->type.pio.sgl_pair_index
            );

         this_request->type.pio.request_current.sgl_set = SCU_SGL_ELEMENT_PAIR_A;

         current_sgl = &(this_request->type.pio.request_current.sgl_pair->A);
      }
   }

   return current_sgl;
}

/**
 * This method will construct the SATA PIO request.
 *
 * @param[in] scic_io_request The core request object which is cast to a SATA
 *            PIO request object.
 *
 * @return This method returns an indication as to whether the construction
 *         was successful.
 * @retval SCI_SUCCESS Currently this method always returns this value.
 */
SCI_STATUS scic_sds_stp_pio_request_construct(
   SCIC_SDS_REQUEST_T  * scic_io_request,
   U8                    sat_protocol,
   BOOL                  copy_rx_frame
)
{
   SCIC_SDS_STP_REQUEST_T * this_request;

   this_request = (SCIC_SDS_STP_REQUEST_T *)scic_io_request;

   scic_sds_stp_non_ncq_request_construct(&this_request->parent);

   scu_stp_raw_request_construct_task_context(
      this_request, this_request->parent.task_context_buffer
   );

   this_request->type.pio.current_transfer_bytes = 0;
   this_request->type.pio.ending_error = 0;
   this_request->type.pio.ending_status = 0;

   this_request->type.pio.request_current.sgl_offset = 0;
   this_request->type.pio.request_current.sgl_set = SCU_SGL_ELEMENT_PAIR_A;
   this_request->type.pio.sat_protocol = sat_protocol;
   this_request->type.pio.sgl_pair_index = 0;

   if ((copy_rx_frame) || (sat_protocol == SAT_PROTOCOL_PIO_DATA_OUT))
   {
      scic_sds_request_build_sgl(&this_request->parent);
      // Since the IO request copy of the TC contains the same data as
      // the actual TC this pointer is vaild for either.
      this_request->type.pio.request_current.sgl_pair =
         &this_request->parent.task_context_buffer->sgl_pair_ab;
   }
   else
   {
      // The user does not want the data copied to the SGL buffer location
      this_request->type.pio.request_current.sgl_pair = NULL;
   }

   sci_base_state_machine_construct(
      &this_request->parent.started_substate_machine,
      &this_request->parent.parent.parent,
      scic_sds_stp_request_started_pio_substate_table,
      SCIC_SDS_STP_REQUEST_STARTED_PIO_AWAIT_H2D_COMPLETION_SUBSTATE
   );

   return SCI_SUCCESS;
}

//******************************************************************************
//* STP NON-DATA STATE MACHINE
//******************************************************************************

/**
 * This method processes a TC completion.  The expected TC completion is
 * for the transmission of the H2D register FIS containing the SATA/STP
 * non-data request.
 *
 * @param[in] this_request
 * @param[in] completion_code
 *
 * @return This method always successfully processes the TC completion.
 * @retval SCI_SUCCESS This value is always returned.
 */
static
SCI_STATUS scic_sds_stp_request_non_data_await_h2d_tc_completion_handler(
   SCIC_SDS_REQUEST_T * this_request,
   U32                  completion_code
)
{
   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_request),
      SCIC_LOG_OBJECT_STP_IO_REQUEST,
      "scic_sds_stp_request_non_data_await_h2d_tc_completion_handler(0x%x, 0x%x) enter\n",
      this_request, completion_code
   ));

   switch (SCU_GET_COMPLETION_TL_STATUS(completion_code))
   {
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_GOOD):
      scic_sds_request_set_status(
         this_request, SCU_TASK_DONE_GOOD, SCI_SUCCESS
      );

      sci_base_state_machine_change_state(
         &this_request->started_substate_machine,
         SCIC_SDS_STP_REQUEST_STARTED_NON_DATA_AWAIT_D2H_SUBSTATE
      );
      break;

   default:
      // All other completion status cause the IO to be complete.  If a NAK
      // was received, then it is up to the user to retry the request.
      scic_sds_request_set_status(
         this_request,
         SCU_NORMALIZE_COMPLETION_STATUS(completion_code),
         SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR
      );

      sci_base_state_machine_change_state(
         &this_request->parent.state_machine, SCI_BASE_REQUEST_STATE_COMPLETED
      );
      break;
   }

   return SCI_SUCCESS;
}

/**
 * This method processes frames received from the target while waiting
 * for a device to host register FIS.  If a non-register FIS is received
 * during this time, it is treated as a protocol violation from an
 * IO perspective.
 *
 * @param[in] request This parameter specifies the request for which a
 *            frame has been received.
 * @param[in] frame_index This parameter specifies the index of the frame
 *            that has been received.
 *
 * @return Indicate if the received frame was processed successfully.
 */
static
SCI_STATUS scic_sds_stp_request_non_data_await_d2h_frame_handler(
   SCIC_SDS_REQUEST_T * request,
   U32                  frame_index
)
{
   SCI_STATUS               status;
   SATA_FIS_HEADER_T      * frame_header;
   U32                    * frame_buffer;
   SCIC_SDS_STP_REQUEST_T * this_request = (SCIC_SDS_STP_REQUEST_T *)request;

   // Save off the controller, so that we do not touch the request after it
   //  is completed.
   SCIC_SDS_CONTROLLER_T  * owning_controller = this_request->parent.owning_controller;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_request),
      SCIC_LOG_OBJECT_STP_IO_REQUEST,
      "scic_sds_stp_request_non_data_await_d2h_frame_handler(0x%x, 0x%x) enter\n",
      this_request, frame_index
   ));

   status = scic_sds_unsolicited_frame_control_get_header(
               &(owning_controller->uf_control),
               frame_index,
               (void**) &frame_header
            );

   if (status == SCI_SUCCESS)
   {
      switch (frame_header->fis_type)
      {
      case SATA_FIS_TYPE_REGD2H:
         scic_sds_unsolicited_frame_control_get_buffer(
            &(owning_controller->uf_control),
            frame_index,
            (void**) &frame_buffer
         );

         scic_sds_controller_copy_sata_response(
            &this_request->d2h_reg_fis, (U32 *)frame_header, frame_buffer
         );

         // The command has completed with error
         scic_sds_request_set_status(
            &this_request->parent,
            SCU_TASK_DONE_CHECK_RESPONSE,
            SCI_FAILURE_IO_RESPONSE_VALID
         );
         break;

      default:
         SCIC_LOG_WARNING((
            sci_base_object_get_logger(this_request),
            SCIC_LOG_OBJECT_STP_IO_REQUEST,
            "IO Request:0x%x Frame Id:%d protocol violation occurred\n",
            this_request, frame_index
         ));

         scic_sds_request_set_status(
            &this_request->parent,
            SCU_TASK_DONE_UNEXP_FIS,
            SCI_FAILURE_PROTOCOL_VIOLATION
         );
         break;
      }

      sci_base_state_machine_change_state(
         &this_request->parent.parent.state_machine,
         SCI_BASE_REQUEST_STATE_COMPLETED
      );

      // Frame has been decoded return it to the controller
      scic_sds_controller_release_frame(
         owning_controller, frame_index
      );
   }
   else
   {
      SCIC_LOG_ERROR((
         sci_base_object_get_logger(this_request),
         SCIC_LOG_OBJECT_STP_IO_REQUEST,
         "SCIC IO Request 0x%x could not get frame header for frame index %d, status %x\n",
         this_request, frame_index, status
      ));
   }

   return status;
}

// ---------------------------------------------------------------------------

SCIC_SDS_IO_REQUEST_STATE_HANDLER_T
   scic_sds_stp_request_started_non_data_substate_handler_table
      [SCIC_SDS_STP_REQUEST_STARTED_NON_DATA_MAX_SUBSTATES] =
{
   // SCIC_SDS_STP_REQUEST_STARTED_NON_DATA_AWAIT_H2D_COMPLETION_SUBSTATE
   {
      {
         scic_sds_request_default_start_handler,
         scic_sds_request_started_state_abort_handler,
         scic_sds_request_default_complete_handler,
         scic_sds_request_default_destruct_handler
      },
      scic_sds_stp_request_non_data_await_h2d_tc_completion_handler,
      scic_sds_request_default_event_handler,
      scic_sds_request_default_frame_handler
   },
   // SCIC_SDS_STP_REQUEST_STARTED_NON_DATA_AWAIT_D2H_SUBSTATE
   {
      {
         scic_sds_request_default_start_handler,
         scic_sds_request_started_state_abort_handler,
         scic_sds_request_default_complete_handler,
         scic_sds_request_default_destruct_handler
      },
      scic_sds_request_default_tc_completion_handler,
      scic_sds_request_default_event_handler,
      scic_sds_stp_request_non_data_await_d2h_frame_handler
   }
};

static
void scic_sds_stp_request_started_non_data_await_h2d_completion_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REQUEST_T *this_request = (SCIC_SDS_REQUEST_T *)object;

   SET_STATE_HANDLER(
      this_request,
      scic_sds_stp_request_started_non_data_substate_handler_table,
      SCIC_SDS_STP_REQUEST_STARTED_NON_DATA_AWAIT_H2D_COMPLETION_SUBSTATE
   );

   scic_sds_remote_device_set_working_request(
      this_request->target_device, this_request
   );
}

static
void scic_sds_stp_request_started_non_data_await_d2h_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REQUEST_T *this_request = (SCIC_SDS_REQUEST_T *)object;

   SET_STATE_HANDLER(
      this_request,
      scic_sds_stp_request_started_non_data_substate_handler_table,
      SCIC_SDS_STP_REQUEST_STARTED_NON_DATA_AWAIT_D2H_SUBSTATE
   );
}

// ---------------------------------------------------------------------------

SCI_BASE_STATE_T scic_sds_stp_request_started_non_data_substate_table
[SCIC_SDS_STP_REQUEST_STARTED_NON_DATA_MAX_SUBSTATES] =
{
   {
      SCIC_SDS_STP_REQUEST_STARTED_NON_DATA_AWAIT_H2D_COMPLETION_SUBSTATE,
      scic_sds_stp_request_started_non_data_await_h2d_completion_enter,
      NULL
   },
   {
      SCIC_SDS_STP_REQUEST_STARTED_NON_DATA_AWAIT_D2H_SUBSTATE,
      scic_sds_stp_request_started_non_data_await_d2h_enter,
      NULL
   }
};

//******************************************************************************
//* STP PIO STATE MACHINE
//******************************************************************************

#define SCU_MAX_FRAME_BUFFER_SIZE  0x400  // 1K is the maximum SCU frame data payload

/**
 * This function will transmit DATA_FIS from (current sgl + offset) for input parameter length.
 * current sgl and offset is alreay stored in the IO request
 *
 * @param[in] this_request
 * @param[in] length
 *
 * @return SCI_STATUS
 */
static
SCI_STATUS scic_sds_stp_request_pio_data_out_trasmit_data_frame (
   SCIC_SDS_REQUEST_T * this_request,
   U32                  length
)
{
   SCI_STATUS status = SCI_SUCCESS;
   SCU_SGL_ELEMENT_T *  current_sgl;
   SCIC_SDS_STP_REQUEST_T * this_sds_stp_request = (SCIC_SDS_STP_REQUEST_T *)this_request;

   // Recycle the TC and reconstruct it for sending out DATA FIS containing
   // for the data from current_sgl+offset for the input length
   SCU_TASK_CONTEXT_T * task_context = scic_sds_controller_get_task_context_buffer(
                                          this_request->owning_controller,
                                          this_request->io_tag
                                       );

   if (this_sds_stp_request->type.pio.request_current.sgl_set == SCU_SGL_ELEMENT_PAIR_A)
   {
      current_sgl = &(this_sds_stp_request->type.pio.request_current.sgl_pair->A);
   }
   else
   {
      current_sgl = &(this_sds_stp_request->type.pio.request_current.sgl_pair->B);
   }

   //update the TC
   task_context->command_iu_upper = current_sgl->address_upper;
   task_context->command_iu_lower = current_sgl->address_lower;
   task_context->transfer_length_bytes = length;
   task_context->type.stp.fis_type = SATA_FIS_TYPE_DATA;

   // send the new TC out.
   status = this_request->owning_controller->state_handlers->parent.continue_io_handler(
      &this_request->owning_controller->parent,
      &this_request->target_device->parent,
      &this_request->parent
   );

   return status;

}

/**
 *
 *
 * @param[in] this_request
 *
 * @return SCI_STATUS
 */
static
SCI_STATUS scic_sds_stp_request_pio_data_out_transmit_data(
   SCIC_SDS_REQUEST_T * this_sds_request
)
{

   SCU_SGL_ELEMENT_T *  current_sgl;
   U32                  sgl_offset;
   U32                  remaining_bytes_in_current_sgl = 0;
   SCI_STATUS           status = SCI_SUCCESS;

   SCIC_SDS_STP_REQUEST_T * this_sds_stp_request = (SCIC_SDS_STP_REQUEST_T *)this_sds_request;

   sgl_offset = this_sds_stp_request->type.pio.request_current.sgl_offset;

   if (this_sds_stp_request->type.pio.request_current.sgl_set == SCU_SGL_ELEMENT_PAIR_A)
   {
      current_sgl = &(this_sds_stp_request->type.pio.request_current.sgl_pair->A);
      remaining_bytes_in_current_sgl = this_sds_stp_request->type.pio.request_current.sgl_pair->A.length - sgl_offset;
   }
   else
   {
      current_sgl = &(this_sds_stp_request->type.pio.request_current.sgl_pair->B);
      remaining_bytes_in_current_sgl = this_sds_stp_request->type.pio.request_current.sgl_pair->B.length - sgl_offset;
   }


   if (this_sds_stp_request->type.pio.pio_transfer_bytes > 0)
   {
      if (this_sds_stp_request->type.pio.pio_transfer_bytes >= remaining_bytes_in_current_sgl )
      {
         //recycle the TC and send the H2D Data FIS from (current sgl + sgl_offset) and length = remaining_bytes_in_current_sgl
         status = scic_sds_stp_request_pio_data_out_trasmit_data_frame (this_sds_request, remaining_bytes_in_current_sgl);
         if (status == SCI_SUCCESS)
         {
            this_sds_stp_request->type.pio.pio_transfer_bytes -= remaining_bytes_in_current_sgl;
            sgl_offset = 0;
         }
      }
      else if (this_sds_stp_request->type.pio.pio_transfer_bytes < remaining_bytes_in_current_sgl )
      {
         //recycle the TC and send the H2D Data FIS from (current sgl + sgl_offset) and length = type.pio.pio_transfer_bytes
         scic_sds_stp_request_pio_data_out_trasmit_data_frame (this_sds_request, this_sds_stp_request->type.pio.pio_transfer_bytes);

         if (status == SCI_SUCCESS)
         {
            //Sgl offset will be adjusted and saved for future
            sgl_offset += this_sds_stp_request->type.pio.pio_transfer_bytes;
            current_sgl->address_lower += this_sds_stp_request->type.pio.pio_transfer_bytes;
            this_sds_stp_request->type.pio.pio_transfer_bytes = 0;
         }
      }
   }

   if (status == SCI_SUCCESS)
   {
      this_sds_stp_request->type.pio.request_current.sgl_offset = sgl_offset;
   }

   return status;
}

/**
 * Copy the data from the buffer for the length specified to the IO reqeust
 * SGL specified data region.
 *
 * @param[in] this_request The request that is used for the SGL processing.
 * @param[in] data_buffer The buffer of data to be copied.
 * @param[in] length  The length of the data transfer.
 *
 * @return SCI_STATUS
 */
static
SCI_STATUS scic_sds_stp_request_pio_data_in_copy_data_buffer(
   SCIC_SDS_STP_REQUEST_T * this_request,
   U8                     * data_buffer,
   U32                      length
)
{
   SCI_STATUS          status;
   SCU_SGL_ELEMENT_T * current_sgl;
   U32                 sgl_offset;
   U32                 data_offset;
   U8                * source_address;

   // Initial setup to get the current working SGL and the offset within the buffer
   current_sgl =
      (this_request->type.pio.request_current.sgl_set == SCU_SGL_ELEMENT_PAIR_A) ?
         &(this_request->type.pio.request_current.sgl_pair->A) :
         &(this_request->type.pio.request_current.sgl_pair->B) ;

   sgl_offset = this_request->type.pio.request_current.sgl_offset;

   source_address = data_buffer;
   data_offset = this_request->type.pio.current_transfer_bytes;
   status = SCI_SUCCESS;

   // While we are still doing Ok and there is more data to transfer
   while (
            (length > 0)
         && (status == SCI_SUCCESS)
         )
   {
      if (current_sgl->length == sgl_offset)
      {
         // This SGL has been exauhasted so we need to get the next SGL
         current_sgl = scic_sds_stp_request_pio_get_next_sgl(this_request);

         if (current_sgl == NULL)
            status = SCI_FAILURE;
         else
            sgl_offset = 0;
      }
      else
      {
#ifdef ENABLE_OSSL_COPY_BUFFER
         scic_cb_io_request_copy_buffer(this_request, data_buffer, data_offset, length);
         length = 0;
#else
         U8 * destination_address;
         U32  copy_length;

         destination_address = (U8 *)scic_cb_io_request_get_virtual_address_from_sgl(
            this_request,
            data_offset
         );

         copy_length = MIN(length, current_sgl->length - sgl_offset);

         memcpy(destination_address, source_address, copy_length);

         length -= copy_length;
         sgl_offset += copy_length;
         data_offset += copy_length;
         source_address += copy_length;
#endif
      }
   }

   this_request->type.pio.request_current.sgl_offset = sgl_offset;

   return status;
}

/**
 * Copy the data buffer to the io request data region.
 *
 * @param[in] this_request The PIO DATA IN request that is to receive the
 *       data.
 * @param[in] data_buffer The buffer to copy from.
 *
 * @return SCI_STATUS
 */
static
SCI_STATUS scic_sds_stp_request_pio_data_in_copy_data(
   SCIC_SDS_STP_REQUEST_T * this_request,
   U8                     * data_buffer
)
{
   SCI_STATUS status;

   // If there is less than 1K remaining in the transfer request
   // copy just the data for the transfer
   if (this_request->type.pio.pio_transfer_bytes < SCU_MAX_FRAME_BUFFER_SIZE)
   {
      status = scic_sds_stp_request_pio_data_in_copy_data_buffer(
         this_request,data_buffer,this_request->type.pio.pio_transfer_bytes);

      if (status == SCI_SUCCESS)
      {
         // All data for this PIO request has now been copied, so we don't
         //  technically need to update current_transfer_bytes here - just
         //  doing it for completeness.
         this_request->type.pio.current_transfer_bytes += this_request->type.pio.pio_transfer_bytes;
         this_request->type.pio.pio_transfer_bytes = 0;
      }
   }
   else
   {
      // We are transferring the whole frame so copy
      status = scic_sds_stp_request_pio_data_in_copy_data_buffer(
         this_request, data_buffer, SCU_MAX_FRAME_BUFFER_SIZE);

      if (status == SCI_SUCCESS)
      {
         this_request->type.pio.pio_transfer_bytes -= SCU_MAX_FRAME_BUFFER_SIZE;
         this_request->type.pio.current_transfer_bytes += SCU_MAX_FRAME_BUFFER_SIZE;
      }
   }

   return status;
}

/**
 *
 *
 * @param[in] this_request
 * @param[in] completion_code
 *
 * @return SCI_STATUS
 */
static
SCI_STATUS scic_sds_stp_request_pio_await_h2d_completion_tc_completion_handler(
   SCIC_SDS_REQUEST_T * this_request,
   U32                  completion_code
)
{
   SCI_STATUS status = SCI_SUCCESS;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_request),
      SCIC_LOG_OBJECT_STP_IO_REQUEST,
      "scic_sds_stp_request_pio_data_in_await_h2d_completion_tc_completion_handler(0x%x, 0x%x) enter\n",
      this_request, completion_code
   ));

   switch (SCU_GET_COMPLETION_TL_STATUS(completion_code))
   {
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_GOOD):
      scic_sds_request_set_status(
         this_request, SCU_TASK_DONE_GOOD, SCI_SUCCESS
      );

      sci_base_state_machine_change_state(
         &this_request->started_substate_machine,
         SCIC_SDS_STP_REQUEST_STARTED_PIO_AWAIT_FRAME_SUBSTATE
      );
      break;

   default:
      // All other completion status cause the IO to be complete.  If a NAK
      // was received, then it is up to the user to retry the request.
      scic_sds_request_set_status(
         this_request,
         SCU_NORMALIZE_COMPLETION_STATUS(completion_code),
         SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR
      );

      sci_base_state_machine_change_state(
         &this_request->parent.state_machine,
         SCI_BASE_REQUEST_STATE_COMPLETED
      );
      break;
   }

   return status;
}

/**
 *
 *
 * @param[in] this_request
 * @param[in] frame_index
 *
 * @return SCI_STATUS
 */
static
SCI_STATUS scic_sds_stp_request_pio_await_frame_frame_handler(
   SCIC_SDS_REQUEST_T * request,
   U32                  frame_index
)
{
   SCI_STATUS               status;
   SATA_FIS_HEADER_T      * frame_header;
   U32                    * frame_buffer;
   SCIC_SDS_STP_REQUEST_T * this_request;
   SCIC_SDS_CONTROLLER_T  * owning_controller;

   this_request = (SCIC_SDS_STP_REQUEST_T *)request;

   // Save off the controller, so that we do not touch the request after it
   //  is completed.
   owning_controller = this_request->parent.owning_controller;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_request),
      SCIC_LOG_OBJECT_STP_IO_REQUEST,
      "scic_sds_stp_request_pio_data_in_await_frame_frame_handler(0x%x, 0x%x) enter\n",
      this_request, frame_index
   ));

   status = scic_sds_unsolicited_frame_control_get_header(
      &(owning_controller->uf_control),
      frame_index,
      (void**) &frame_header
   );

   if (status == SCI_SUCCESS)
   {
      switch (frame_header->fis_type)
      {
      case SATA_FIS_TYPE_PIO_SETUP:
         // Get from the frame buffer the PIO Setup Data
         scic_sds_unsolicited_frame_control_get_buffer(
            &(owning_controller->uf_control),
            frame_index,
            (void**) &frame_buffer
         );

         // Get the data from the PIO Setup
         // The SCU Hardware returns first word in the frame_header and the rest
         // of the data is in the frame buffer so we need to back up one dword
         this_request->type.pio.pio_transfer_bytes =
            (U16)((SATA_FIS_PIO_SETUP_T *)(&frame_buffer[-1]))->transfter_count;
         this_request->type.pio.ending_status =
            (U8)((SATA_FIS_PIO_SETUP_T *)(&frame_buffer[-1]))->ending_status;

         scic_sds_controller_copy_sata_response(
            &this_request->d2h_reg_fis, (U32 *)frame_header, frame_buffer
         );

         this_request->d2h_reg_fis.status =
            this_request->type.pio.ending_status;

         //The next state is dependent on whether the request was PIO Data-in or Data out
         if (this_request->type.pio.sat_protocol == SAT_PROTOCOL_PIO_DATA_IN)
         {
         sci_base_state_machine_change_state(
            &this_request->parent.started_substate_machine,
            SCIC_SDS_STP_REQUEST_STARTED_PIO_DATA_IN_AWAIT_DATA_SUBSTATE
            );
         }
         else if (this_request->type.pio.sat_protocol == SAT_PROTOCOL_PIO_DATA_OUT)
         {
            //Transmit data
            status = scic_sds_stp_request_pio_data_out_transmit_data ( request);
            if (status == SCI_SUCCESS)
            {
               sci_base_state_machine_change_state(
                  &this_request->parent.started_substate_machine,
                  SCIC_SDS_STP_REQUEST_STARTED_PIO_DATA_OUT_TRANSMIT_DATA_SUBSTATE
               );
            }
         }
      break;

      case SATA_FIS_TYPE_SETDEVBITS:
         sci_base_state_machine_change_state(
            &this_request->parent.started_substate_machine,
            SCIC_SDS_STP_REQUEST_STARTED_PIO_AWAIT_FRAME_SUBSTATE
            );
      break;

      case SATA_FIS_TYPE_REGD2H:
         if ( (frame_header->status & ATA_STATUS_REG_BSY_BIT) == 0)
         {
            scic_sds_unsolicited_frame_control_get_buffer(
               &(owning_controller->uf_control),
               frame_index,
               (void**) &frame_buffer
            );

            scic_sds_controller_copy_sata_response(
               &this_request->d2h_reg_fis, (U32 *)frame_header, frame_buffer);

            scic_sds_request_set_status(
               &this_request->parent,
               SCU_TASK_DONE_CHECK_RESPONSE,
               SCI_FAILURE_IO_RESPONSE_VALID
            );

            sci_base_state_machine_change_state(
               &this_request->parent.parent.state_machine,
               SCI_BASE_REQUEST_STATE_COMPLETED
            );
         }
         else
         {
            // Now why is the drive sending a D2H Register FIS when it is still busy?
            // Do nothing since we are still in the right state.
            SCIC_LOG_INFO((
               sci_base_object_get_logger(this_request),
               SCIC_LOG_OBJECT_STP_IO_REQUEST,
               "SCIC PIO Request 0x%x received D2H Register FIS with BSY status 0x%x\n",
               this_request, frame_header->status
            ));
         }
         break;

         default:
         break;
         }

      // Frame is decoded return it to the controller
      scic_sds_controller_release_frame(
         owning_controller,
         frame_index
      );
   }
   else
   {
      SCIC_LOG_ERROR((
         sci_base_object_get_logger(this_request),
         SCIC_LOG_OBJECT_STP_IO_REQUEST,
         "SCIC IO Request 0x%x could not get frame header for frame index %d, status %x\n",
         this_request, frame_index, status
      ));
   }

   return status;
}

/**
 *
 *
 * @param[in] this_request
 * @param[in] frame_index
 *
 * @return SCI_STATUS
 */
static
SCI_STATUS scic_sds_stp_request_pio_data_in_await_data_frame_handler(
   SCIC_SDS_REQUEST_T * request,
   U32                  frame_index
)
{
   SCI_STATUS               status;
   SATA_FIS_HEADER_T      * frame_header;
   SATA_FIS_DATA_T        * frame_buffer;
   SCIC_SDS_STP_REQUEST_T * this_request;
   SCIC_SDS_CONTROLLER_T  * owning_controller;

   this_request = (SCIC_SDS_STP_REQUEST_T *)request;

   // Save off the controller, so that we do not touch the request after it
   //  is completed.
   owning_controller = this_request->parent.owning_controller;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_request),
      SCIC_LOG_OBJECT_STP_IO_REQUEST,
      "scic_sds_stp_request_pio_data_in_await_data_frame_handler(0x%x, 0x%x) enter\n",
      this_request, frame_index
   ));

   status = scic_sds_unsolicited_frame_control_get_header(
      &(owning_controller->uf_control),
      frame_index,
      (void**) &frame_header
   );

   if (status == SCI_SUCCESS)
   {
      if (frame_header->fis_type == SATA_FIS_TYPE_DATA)
      {
         if (this_request->type.pio.request_current.sgl_pair == NULL)
         {
            this_request->parent.saved_rx_frame_index = frame_index;
            this_request->type.pio.pio_transfer_bytes = 0;
         }
         else
         {
            status = scic_sds_unsolicited_frame_control_get_buffer(
               &(owning_controller->uf_control),
               frame_index,
               (void**) &frame_buffer
            );

            status = scic_sds_stp_request_pio_data_in_copy_data(this_request, (U8 *)frame_buffer);

            // Frame is decoded return it to the controller
            scic_sds_controller_release_frame(
               owning_controller,
               frame_index
            );
         }

         // Check for the end of the transfer, are there more bytes remaining
         // for this data transfer
         if (
               (status == SCI_SUCCESS)
            && (this_request->type.pio.pio_transfer_bytes == 0)
            )
         {
            if ((this_request->type.pio.ending_status & ATA_STATUS_REG_BSY_BIT) == 0)
            {
               scic_sds_request_set_status(
                  &this_request->parent,
                  SCU_TASK_DONE_CHECK_RESPONSE,
                  SCI_FAILURE_IO_RESPONSE_VALID
               );

               sci_base_state_machine_change_state(
                  &this_request->parent.parent.state_machine,
                  SCI_BASE_REQUEST_STATE_COMPLETED
               );
            }
            else
            {
               sci_base_state_machine_change_state(
                  &this_request->parent.started_substate_machine,
                  SCIC_SDS_STP_REQUEST_STARTED_PIO_AWAIT_FRAME_SUBSTATE
               );
            }
         }
      }
      else
      {
         SCIC_LOG_ERROR((
            sci_base_object_get_logger(this_request),
            SCIC_LOG_OBJECT_STP_IO_REQUEST,
            "SCIC PIO Request 0x%x received frame %d with fis type 0x%02x when expecting a data fis.\n",
            this_request, frame_index, frame_header->fis_type
         ));

         scic_sds_request_set_status(
            &this_request->parent,
            SCU_TASK_DONE_GOOD,
            SCI_FAILURE_IO_REQUIRES_SCSI_ABORT
         );

         sci_base_state_machine_change_state(
            &this_request->parent.parent.state_machine,
            SCI_BASE_REQUEST_STATE_COMPLETED
         );

         // Frame is decoded return it to the controller
         scic_sds_controller_release_frame(
            owning_controller,
            frame_index
         );
      }
   }
   else
   {
      SCIC_LOG_ERROR((
         sci_base_object_get_logger(this_request),
         SCIC_LOG_OBJECT_STP_IO_REQUEST,
         "SCIC IO Request 0x%x could not get frame header for frame index %d, status %x\n",
         this_request, frame_index, status
      ));
   }

   return status;
}


/**
 *
 *
 * @param[in] this_request
 * @param[in] completion_code
 *
 * @return SCI_STATUS
 */
static
SCI_STATUS scic_sds_stp_request_pio_data_out_await_data_transmit_completion_tc_completion_handler(

   SCIC_SDS_REQUEST_T * this_request,
   U32                  completion_code
)
{
   SCI_STATUS  status                     = SCI_SUCCESS;
   BOOL        all_frames_transferred     = FALSE;

   SCIC_SDS_STP_REQUEST_T *this_scic_sds_stp_request = (SCIC_SDS_STP_REQUEST_T *) this_request;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_request),
      SCIC_LOG_OBJECT_STP_IO_REQUEST,
      "scic_sds_stp_request_pio_data_in_await_h2d_completion_tc_completion_handler(0x%x, 0x%x) enter\n",
      this_request, completion_code
   ));

   switch (SCU_GET_COMPLETION_TL_STATUS(completion_code))
   {
      case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_GOOD):
         //Transmit data
         if (this_scic_sds_stp_request->type.pio.pio_transfer_bytes != 0)
         {
            status = scic_sds_stp_request_pio_data_out_transmit_data ( this_request);
            if (status == SCI_SUCCESS)
            {
               if (this_scic_sds_stp_request->type.pio.pio_transfer_bytes == 0)
               all_frames_transferred = TRUE;
            }
         }
         else if (this_scic_sds_stp_request->type.pio.pio_transfer_bytes == 0)
         {
            //this will happen if the all data is written at the first time after the pio setup fis is received
            all_frames_transferred  = TRUE;
         }

         //all data transferred.
         if (all_frames_transferred)
         {
            //Change the state to SCIC_SDS_STP_REQUEST_STARTED_PIO_DATA_IN_AWAIT_FRAME_SUBSTATE
            //and wait for PIO_SETUP fis / or D2H REg fis.
            sci_base_state_machine_change_state(
               &this_request->started_substate_machine,
               SCIC_SDS_STP_REQUEST_STARTED_PIO_AWAIT_FRAME_SUBSTATE
            );
         }
         break;

      default:
         // All other completion status cause the IO to be complete.  If a NAK
         // was received, then it is up to the user to retry the request.
         scic_sds_request_set_status(
            this_request,
            SCU_NORMALIZE_COMPLETION_STATUS(completion_code),
            SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR
         );

         sci_base_state_machine_change_state(
            &this_request->parent.state_machine,
            SCI_BASE_REQUEST_STATE_COMPLETED
         );
         break;
   }

   return status;
}

/**
 * This method will handle any link layer events while waiting for the data
 * frame.
 *
 * @param[in] request This is the request which is receiving the event.
 * @param[in] event_code This is the event code that the request on which the
 *       request is expected to take action.
 *
 * @return SCI_STATUS
 * @retval SCI_SUCCESS
 * @retval SCI_FAILURE
 */
static
SCI_STATUS scic_sds_stp_request_pio_data_in_await_data_event_handler(
   SCIC_SDS_REQUEST_T * request,
   U32                  event_code
)
{
   SCI_STATUS status;

   switch (scu_get_event_specifier(event_code))
   {
   case SCU_TASK_DONE_CRC_ERR << SCU_EVENT_SPECIFIC_CODE_SHIFT:
      // We are waiting for data and the SCU has R_ERR the data frame.
      // Go back to waiting for the D2H Register FIS
      sci_base_state_machine_change_state(
         &request->started_substate_machine,
         SCIC_SDS_STP_REQUEST_STARTED_PIO_AWAIT_FRAME_SUBSTATE
      );

      status = SCI_SUCCESS;
      break;

   default:
      SCIC_LOG_ERROR((
         sci_base_object_get_logger(request),
         SCIC_LOG_OBJECT_STP_IO_REQUEST,
         "SCIC PIO Request 0x%x received unexpected event 0x%08x\n",
         request, event_code
      ));

      /// @todo Should we fail the PIO request when we get an unexpected event?
      status = SCI_FAILURE;
      break;
   }

   return status;
}

// ---------------------------------------------------------------------------

SCIC_SDS_IO_REQUEST_STATE_HANDLER_T
   scic_sds_stp_request_started_pio_substate_handler_table
      [SCIC_SDS_STP_REQUEST_STARTED_PIO_MAX_SUBSTATES] =
{
   // SCIC_SDS_STP_REQUEST_STARTED_PIO_AWAIT_H2D_COMPLETION_SUBSTATE
   {
      {
         scic_sds_request_default_start_handler,
         scic_sds_request_started_state_abort_handler,
         scic_sds_request_default_complete_handler,
         scic_sds_request_default_destruct_handler
      },
      scic_sds_stp_request_pio_await_h2d_completion_tc_completion_handler,
      scic_sds_request_default_event_handler,
      scic_sds_request_default_frame_handler
   },
   // SCIC_SDS_STP_REQUEST_STARTED_PIO_AWAIT_FRAME_SUBSTATE
   {
      {
         scic_sds_request_default_start_handler,
         //scic_sds_stp_pio_request_data_in_await_frame_abort_handler,
         scic_sds_request_started_state_abort_handler,
         scic_sds_request_default_complete_handler,
         scic_sds_request_default_destruct_handler
      },
      scic_sds_request_default_tc_completion_handler,
      scic_sds_request_default_event_handler,
      scic_sds_stp_request_pio_await_frame_frame_handler
   },
   // SCIC_SDS_STP_REQUEST_STARTED_PIO_DATA_IN_AWAIT_DATA_SUBSTATE
   {
      {
         scic_sds_request_default_start_handler,
         //scic_sds_stp_pio_request_data_in_await_data_abort_handler,
         scic_sds_request_started_state_abort_handler,
         scic_sds_request_default_complete_handler,
         scic_sds_request_default_destruct_handler
      },
      scic_sds_request_default_tc_completion_handler,
      scic_sds_stp_request_pio_data_in_await_data_event_handler,
      scic_sds_stp_request_pio_data_in_await_data_frame_handler
   },
   //SCIC_SDS_STP_REQUEST_STARTED_PIO_DATA_OUT_TRANSMIT_DATA_SUBSTATE
   {
      {
         scic_sds_request_default_start_handler,
         scic_sds_request_started_state_abort_handler,
         scic_sds_request_default_complete_handler,
         scic_sds_request_default_destruct_handler
      },
      scic_sds_stp_request_pio_data_out_await_data_transmit_completion_tc_completion_handler,
      scic_sds_request_default_event_handler,
      scic_sds_request_default_frame_handler
   }
};

static
void scic_sds_stp_request_started_pio_await_h2d_completion_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REQUEST_T *this_request = (SCIC_SDS_REQUEST_T *)object;

   SET_STATE_HANDLER(
      this_request,
      scic_sds_stp_request_started_pio_substate_handler_table,
      SCIC_SDS_STP_REQUEST_STARTED_PIO_AWAIT_H2D_COMPLETION_SUBSTATE
   );

   scic_sds_remote_device_set_working_request(
      this_request->target_device, this_request);
}

static
void scic_sds_stp_request_started_pio_await_frame_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REQUEST_T *this_request = (SCIC_SDS_REQUEST_T *)object;

   SET_STATE_HANDLER(
      this_request,
      scic_sds_stp_request_started_pio_substate_handler_table,
      SCIC_SDS_STP_REQUEST_STARTED_PIO_AWAIT_FRAME_SUBSTATE
   );
}

static
void scic_sds_stp_request_started_pio_data_in_await_data_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REQUEST_T *this_request = (SCIC_SDS_REQUEST_T *)object;

   SET_STATE_HANDLER(
      this_request,
      scic_sds_stp_request_started_pio_substate_handler_table,
      SCIC_SDS_STP_REQUEST_STARTED_PIO_DATA_IN_AWAIT_DATA_SUBSTATE
   );
}

static
void scic_sds_stp_request_started_pio_data_out_transmit_data_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REQUEST_T *this_request = (SCIC_SDS_REQUEST_T *)object;

   SET_STATE_HANDLER(
      this_request,
      scic_sds_stp_request_started_pio_substate_handler_table,
      SCIC_SDS_STP_REQUEST_STARTED_PIO_DATA_OUT_TRANSMIT_DATA_SUBSTATE
   );
}

// ---------------------------------------------------------------------------

SCI_BASE_STATE_T
   scic_sds_stp_request_started_pio_substate_table
      [SCIC_SDS_STP_REQUEST_STARTED_PIO_MAX_SUBSTATES] =
{
   {
      SCIC_SDS_STP_REQUEST_STARTED_PIO_AWAIT_H2D_COMPLETION_SUBSTATE,
      scic_sds_stp_request_started_pio_await_h2d_completion_enter,
      NULL
   },
   {
      SCIC_SDS_STP_REQUEST_STARTED_PIO_AWAIT_FRAME_SUBSTATE,
      scic_sds_stp_request_started_pio_await_frame_enter,
      NULL
   },
   {
      SCIC_SDS_STP_REQUEST_STARTED_PIO_DATA_IN_AWAIT_DATA_SUBSTATE,
      scic_sds_stp_request_started_pio_data_in_await_data_enter,
      NULL
   },
   {
      SCIC_SDS_STP_REQUEST_STARTED_PIO_DATA_OUT_TRANSMIT_DATA_SUBSTATE,
      scic_sds_stp_request_started_pio_data_out_transmit_data_enter,
      NULL
   }
};

//******************************************************************************
//* UDMA REQUEST STATE MACHINE
//******************************************************************************

static
void scic_sds_stp_request_udma_complete_request(
   SCIC_SDS_REQUEST_T * this_request,
   U32                  scu_status,
   SCI_STATUS           sci_status
)
{
   scic_sds_request_set_status(
      this_request, scu_status, sci_status
   );

   sci_base_state_machine_change_state(
      &this_request->parent.state_machine,
      SCI_BASE_REQUEST_STATE_COMPLETED
   );
}

/**
 *
 *
 * @param[in] this_request
 * @param[in] frame_index
 *
 * @return SCI_STATUS
 */
static
SCI_STATUS scic_sds_stp_request_udma_general_frame_handler(
   SCIC_SDS_REQUEST_T * this_request,
   U32                  frame_index
)
{
   SCI_STATUS          status;
   SATA_FIS_HEADER_T * frame_header;
   U32               * frame_buffer;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_request),
      SCIC_LOG_OBJECT_STP_IO_REQUEST,
      "scic_sds_stp_pio_request_data_in_await_frame_frame_handler(0x%x, 0x%x) enter\n",
      this_request, frame_index
   ));

   status = scic_sds_unsolicited_frame_control_get_header(
      &this_request->owning_controller->uf_control,
      frame_index,
      (void**) &frame_header
   );

   if (
         (status == SCI_SUCCESS)
      && (frame_header->fis_type == SATA_FIS_TYPE_REGD2H)
      )
   {
      scic_sds_unsolicited_frame_control_get_buffer(
         &this_request->owning_controller->uf_control,
         frame_index,
         (void**) &frame_buffer
      );

      scic_sds_controller_copy_sata_response(
         &((SCIC_SDS_STP_REQUEST_T *)this_request)->d2h_reg_fis,
         (U32 *)frame_header,
         frame_buffer
      );
   }

   scic_sds_controller_release_frame(
      this_request->owning_controller, frame_index);

   return status;
}

/**
 * @brief This method process TC completions while in the state where
 *        we are waiting for TC completions.
 *
 * @param[in] this_request
 * @param[in] completion_code
 *
 * @return SCI_STATUS
 */
static
SCI_STATUS scic_sds_stp_request_udma_await_tc_completion_tc_completion_handler(
   SCIC_SDS_REQUEST_T * request,
   U32                  completion_code
)
{
   SCI_STATUS               status = SCI_SUCCESS;
   SCIC_SDS_STP_REQUEST_T * this_request = (SCIC_SDS_STP_REQUEST_T *)request;

   switch (SCU_GET_COMPLETION_TL_STATUS(completion_code))
   {
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_GOOD):
      scic_sds_stp_request_udma_complete_request(
         &this_request->parent, SCU_TASK_DONE_GOOD, SCI_SUCCESS
      );
   break;

   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_UNEXP_FIS):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_REG_ERR):
      // We must check ther response buffer to see if the D2H Register FIS was
      // received before we got the TC completion.
      if (this_request->d2h_reg_fis.fis_type == SATA_FIS_TYPE_REGD2H)
      {
         scic_sds_remote_device_suspend(
            this_request->parent.target_device,
            SCU_EVENT_SPECIFIC(SCU_NORMALIZE_COMPLETION_STATUS(completion_code))
         );

         scic_sds_stp_request_udma_complete_request(
            &this_request->parent,
            SCU_TASK_DONE_CHECK_RESPONSE,
            SCI_FAILURE_IO_RESPONSE_VALID
         );
      }
      else
      {
         // If we have an error completion status for the TC then we can expect a
         // D2H register FIS from the device so we must change state to wait for it
         sci_base_state_machine_change_state(
            &this_request->parent.started_substate_machine,
            SCIC_SDS_STP_REQUEST_STARTED_UDMA_AWAIT_D2H_REG_FIS_SUBSTATE
         );
      }
   break;

   /// @todo Check to see if any of these completion status need to wait for
   ///       the device to host register fis.
   /// @todo We can retry the command for SCU_TASK_DONE_CMD_LL_R_ERR - this comes only for B0
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_INV_FIS_LEN):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_MAX_PLD_ERR):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_LL_R_ERR):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_CMD_LL_R_ERR):
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_CRC_ERR):
      scic_sds_remote_device_suspend(
         this_request->parent.target_device,
         SCU_EVENT_SPECIFIC(SCU_NORMALIZE_COMPLETION_STATUS(completion_code))
      );
      // Fall through to the default case
   default:
      // All other completion status cause the IO to be complete.
      SCIC_LOG_ERROR((
         sci_base_object_get_logger(&this_request->parent),
         SCIC_LOG_OBJECT_STP_IO_REQUEST,
         "SCIC IO Request 0x%x returning CONTROLLER_SPECIFIC_IO_ERR for completion code 0x%x\n",
         &this_request->parent, completion_code
      ));
      scic_sds_stp_request_udma_complete_request(
         &this_request->parent,
         SCU_NORMALIZE_COMPLETION_STATUS(completion_code),
         SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR
      );
      break;
   }

   return status;
}

static
SCI_STATUS scic_sds_stp_request_udma_await_d2h_reg_fis_frame_handler(
   SCIC_SDS_REQUEST_T * this_request,
   U32                  frame_index
)
{
   SCI_STATUS status;

   // Use the general frame handler to copy the resposne data
   status = scic_sds_stp_request_udma_general_frame_handler(this_request, frame_index);

   if (status == SCI_SUCCESS)
   {
      scic_sds_stp_request_udma_complete_request(
         this_request,
         SCU_TASK_DONE_CHECK_RESPONSE,
         SCI_FAILURE_IO_RESPONSE_VALID
      );
   }

   return status;
}

// ---------------------------------------------------------------------------

SCIC_SDS_IO_REQUEST_STATE_HANDLER_T
   scic_sds_stp_request_started_udma_substate_handler_table
      [SCIC_SDS_STP_REQUEST_STARTED_UDMA_MAX_SUBSTATES] =
{
   // SCIC_SDS_STP_REQUEST_STARTED_UDMA_AWAIT_TC_COMPLETION_SUBSTATE
   {
      {
         scic_sds_request_default_start_handler,
         scic_sds_request_started_state_abort_handler,
         scic_sds_request_default_complete_handler,
         scic_sds_request_default_destruct_handler
      },
      scic_sds_stp_request_udma_await_tc_completion_tc_completion_handler,
      scic_sds_request_default_event_handler,
      scic_sds_stp_request_udma_general_frame_handler
   },
   // SCIC_SDS_STP_REQUEST_STARTED_UDMA_AWAIT_D2H_REG_FIS_SUBSTATE
   {
      {
         scic_sds_request_default_start_handler,
         scic_sds_request_started_state_abort_handler,
         scic_sds_request_default_complete_handler,
         scic_sds_request_default_destruct_handler
      },
      scic_sds_request_default_tc_completion_handler,
      scic_sds_request_default_event_handler,
      scic_sds_stp_request_udma_await_d2h_reg_fis_frame_handler
   }
};

/**
 *
 *
 * @param[in] object
 */
static
void scic_sds_stp_request_started_udma_await_tc_completion_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REQUEST_T *this_request = (SCIC_SDS_REQUEST_T *)object;

   SET_STATE_HANDLER(
      this_request,
      scic_sds_stp_request_started_udma_substate_handler_table,
      SCIC_SDS_STP_REQUEST_STARTED_UDMA_AWAIT_TC_COMPLETION_SUBSTATE
   );
}

/**
 * This state is entered when there is an TC completion failure.  The hardware
 * received an unexpected condition while processing the IO request and now
 * will UF the D2H register FIS to complete the IO.
 *
 * @param[in] object
 */
static
void scic_sds_stp_request_started_udma_await_d2h_reg_fis_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REQUEST_T *this_request = (SCIC_SDS_REQUEST_T *)object;

   SET_STATE_HANDLER(
      this_request,
      scic_sds_stp_request_started_udma_substate_handler_table,
      SCIC_SDS_STP_REQUEST_STARTED_UDMA_AWAIT_D2H_REG_FIS_SUBSTATE
   );
}

// ---------------------------------------------------------------------------

SCI_BASE_STATE_T
   scic_sds_stp_request_started_udma_substate_table
      [SCIC_SDS_STP_REQUEST_STARTED_UDMA_MAX_SUBSTATES] =
{
   {
      SCIC_SDS_STP_REQUEST_STARTED_UDMA_AWAIT_TC_COMPLETION_SUBSTATE,
      scic_sds_stp_request_started_udma_await_tc_completion_enter,
      NULL
   },
   {
      SCIC_SDS_STP_REQUEST_STARTED_UDMA_AWAIT_D2H_REG_FIS_SUBSTATE,
      scic_sds_stp_request_started_udma_await_d2h_reg_fis_enter,
      NULL
   }
};

//******************************************************************************
//* STP SOFT RESET STATE MACHINE
//******************************************************************************

/**
 * This method processes a TC completion.  The expected TC completion is
 * for the transmission of the H2D register FIS containing the SATA/STP
 * non-data request.
 *
 * @param[in] this_request
 * @param[in] completion_code
 *
 * @return This method always successfully processes the TC completion.
 * @retval SCI_SUCCESS This value is always returned.
 */
static
SCI_STATUS scic_sds_stp_request_soft_reset_await_h2d_asserted_tc_completion_handler(
   SCIC_SDS_REQUEST_T * this_request,
   U32                  completion_code
)
{
   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_request),
      SCIC_LOG_OBJECT_STP_IO_REQUEST,
      "scic_sds_stp_request_soft_reset_await_h2d_tc_completion_handler(0x%x, 0x%x) enter\n",
      this_request, completion_code
   ));

   switch (SCU_GET_COMPLETION_TL_STATUS(completion_code))
   {
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_GOOD):
      scic_sds_request_set_status(
         this_request, SCU_TASK_DONE_GOOD, SCI_SUCCESS
      );

      sci_base_state_machine_change_state(
         &this_request->started_substate_machine,
         SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_H2D_DIAGNOSTIC_COMPLETION_SUBSTATE
      );
      break;

   default:
      // All other completion status cause the IO to be complete.  If a NAK
      // was received, then it is up to the user to retry the request.
      scic_sds_request_set_status(
         this_request,
         SCU_NORMALIZE_COMPLETION_STATUS(completion_code),
         SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR
      );

      sci_base_state_machine_change_state(
         &this_request->parent.state_machine, SCI_BASE_REQUEST_STATE_COMPLETED
      );
      break;
   }

   return SCI_SUCCESS;
}

/**
 * This method processes a TC completion.  The expected TC completion is
 * for the transmission of the H2D register FIS containing the SATA/STP
 * non-data request.
 *
 * @param[in] this_request
 * @param[in] completion_code
 *
 * @return This method always successfully processes the TC completion.
 * @retval SCI_SUCCESS This value is always returned.
 */
static
SCI_STATUS scic_sds_stp_request_soft_reset_await_h2d_diagnostic_tc_completion_handler(
   SCIC_SDS_REQUEST_T * this_request,
   U32                  completion_code
)
{
   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_request),
      SCIC_LOG_OBJECT_STP_IO_REQUEST,
      "scic_sds_stp_request_soft_reset_await_h2d_tc_completion_handler(0x%x, 0x%x) enter\n",
      this_request, completion_code
   ));

   switch (SCU_GET_COMPLETION_TL_STATUS(completion_code))
   {
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_GOOD):
      scic_sds_request_set_status(
         this_request, SCU_TASK_DONE_GOOD, SCI_SUCCESS
      );

      sci_base_state_machine_change_state(
         &this_request->started_substate_machine,
         SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_D2H_RESPONSE_FRAME_SUBSTATE
      );
      break;

   default:
      // All other completion status cause the IO to be complete.  If a NAK
      // was received, then it is up to the user to retry the request.
      scic_sds_request_set_status(
         this_request,
         SCU_NORMALIZE_COMPLETION_STATUS(completion_code),
         SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR
      );

      sci_base_state_machine_change_state(
         &this_request->parent.state_machine, SCI_BASE_REQUEST_STATE_COMPLETED
      );
      break;
   }

   return SCI_SUCCESS;
}

/**
 * This method processes frames received from the target while waiting
 * for a device to host register FIS.  If a non-register FIS is received
 * during this time, it is treated as a protocol violation from an
 * IO perspective.
 *
 * @param[in] request This parameter specifies the request for which a
 *            frame has been received.
 * @param[in] frame_index This parameter specifies the index of the frame
 *            that has been received.
 *
 * @return Indicate if the received frame was processed successfully.
 */
static
SCI_STATUS scic_sds_stp_request_soft_reset_await_d2h_frame_handler(
   SCIC_SDS_REQUEST_T * request,
   U32                  frame_index
)
{
   SCI_STATUS               status;
   SATA_FIS_HEADER_T      * frame_header;
   U32                    * frame_buffer;
   SCIC_SDS_STP_REQUEST_T * this_request = (SCIC_SDS_STP_REQUEST_T *)request;

   // Save off the controller, so that we do not touch the request after it
   //  is completed.
   SCIC_SDS_CONTROLLER_T  * owning_controller = this_request->parent.owning_controller;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_request),
      SCIC_LOG_OBJECT_STP_IO_REQUEST,
      "scic_sds_stp_request_soft_reset_await_d2h_frame_handler(0x%x, 0x%x) enter\n",
      this_request, frame_index
   ));

   status = scic_sds_unsolicited_frame_control_get_header(
               &(owning_controller->uf_control),
               frame_index,
               (void**) &frame_header
            );

   if (status == SCI_SUCCESS)
   {
      switch (frame_header->fis_type)
      {
      case SATA_FIS_TYPE_REGD2H:
         scic_sds_unsolicited_frame_control_get_buffer(
            &(owning_controller->uf_control),
            frame_index,
            (void**) &frame_buffer
         );

         scic_sds_controller_copy_sata_response(
            &this_request->d2h_reg_fis, (U32 *)frame_header, frame_buffer
         );

         // The command has completed with error
         scic_sds_request_set_status(
            &this_request->parent,
            SCU_TASK_DONE_CHECK_RESPONSE,
            SCI_FAILURE_IO_RESPONSE_VALID
         );
         break;

      default:
         SCIC_LOG_WARNING((
            sci_base_object_get_logger(this_request),
            SCIC_LOG_OBJECT_STP_IO_REQUEST,
            "IO Request:0x%x Frame Id:%d protocol violation occurred\n",
            this_request, frame_index
         ));

         scic_sds_request_set_status(
            &this_request->parent,
            SCU_TASK_DONE_UNEXP_FIS,
            SCI_FAILURE_PROTOCOL_VIOLATION
         );
         break;
      }

      sci_base_state_machine_change_state(
         &this_request->parent.parent.state_machine,
         SCI_BASE_REQUEST_STATE_COMPLETED
      );

      // Frame has been decoded return it to the controller
      scic_sds_controller_release_frame(
         owning_controller, frame_index
      );
   }
   else
   {
      SCIC_LOG_ERROR((
         sci_base_object_get_logger(this_request),
         SCIC_LOG_OBJECT_STP_IO_REQUEST,
         "SCIC IO Request 0x%x could not get frame header for frame index %d, status %x\n",
         this_request, frame_index, status
      ));
   }

   return status;
}

// ---------------------------------------------------------------------------

SCIC_SDS_IO_REQUEST_STATE_HANDLER_T
   scic_sds_stp_request_started_soft_reset_substate_handler_table
      [SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_MAX_SUBSTATES] =
{
   // SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_H2D_ASSERTED_COMPLETION_SUBSTATE
   {
      {
         scic_sds_request_default_start_handler,
         scic_sds_request_started_state_abort_handler,
         scic_sds_request_default_complete_handler,
         scic_sds_request_default_destruct_handler
      },
      scic_sds_stp_request_soft_reset_await_h2d_asserted_tc_completion_handler,
      scic_sds_request_default_event_handler,
      scic_sds_request_default_frame_handler
   },
   // SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_H2D_DIAGNOSTIC_COMPLETION_SUBSTATE
   {
      {
         scic_sds_request_default_start_handler,
         scic_sds_request_started_state_abort_handler,
         scic_sds_request_default_complete_handler,
         scic_sds_request_default_destruct_handler
      },
      scic_sds_stp_request_soft_reset_await_h2d_diagnostic_tc_completion_handler,
      scic_sds_request_default_event_handler,
      scic_sds_request_default_frame_handler
   },
   // SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_D2H_RESPONSE_FRAME_SUBSTATE
   {
      {
         scic_sds_request_default_start_handler,
         scic_sds_request_started_state_abort_handler,
         scic_sds_request_default_complete_handler,
         scic_sds_request_default_destruct_handler
      },
      scic_sds_request_default_tc_completion_handler,
      scic_sds_request_default_event_handler,
      scic_sds_stp_request_soft_reset_await_d2h_frame_handler
   }
};

static
void scic_sds_stp_request_started_soft_reset_await_h2d_asserted_completion_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REQUEST_T *this_request = (SCIC_SDS_REQUEST_T *)object;

   SET_STATE_HANDLER(
      this_request,
      scic_sds_stp_request_started_soft_reset_substate_handler_table,
      SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_H2D_ASSERTED_COMPLETION_SUBSTATE
   );

   scic_sds_remote_device_set_working_request(
      this_request->target_device, this_request
   );
}

static
void scic_sds_stp_request_started_soft_reset_await_h2d_diagnostic_completion_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCI_STATUS status;
   SCIC_SDS_REQUEST_T *this_request = (SCIC_SDS_REQUEST_T *)object;
   SATA_FIS_REG_H2D_T *h2d_fis;
   SCU_TASK_CONTEXT_T *task_context;

   // Clear the SRST bit
   h2d_fis = scic_stp_io_request_get_h2d_reg_address(this_request);
   h2d_fis->control = 0;

   // Clear the TC control bit
   task_context = scic_sds_controller_get_task_context_buffer(
                        this_request->owning_controller, this_request->io_tag);
   task_context->control_frame = 0;

   status = this_request->owning_controller->state_handlers->parent.continue_io_handler(
      &this_request->owning_controller->parent,
      &this_request->target_device->parent,
      &this_request->parent
   );

   if (status == SCI_SUCCESS)
   {
      SET_STATE_HANDLER(
         this_request,
         scic_sds_stp_request_started_soft_reset_substate_handler_table,
         SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_H2D_DIAGNOSTIC_COMPLETION_SUBSTATE
      );
   }
}

static
void scic_sds_stp_request_started_soft_reset_await_d2h_response_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REQUEST_T *this_request = (SCIC_SDS_REQUEST_T *)object;

   SET_STATE_HANDLER(
      this_request,
      scic_sds_stp_request_started_soft_reset_substate_handler_table,
      SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_D2H_RESPONSE_FRAME_SUBSTATE
   );
}

// ---------------------------------------------------------------------------

SCI_BASE_STATE_T
   scic_sds_stp_request_started_soft_reset_substate_table
      [SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_MAX_SUBSTATES] =
{
   {
      SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_H2D_ASSERTED_COMPLETION_SUBSTATE,
      scic_sds_stp_request_started_soft_reset_await_h2d_asserted_completion_enter,
      NULL
   },
   {
      SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_H2D_DIAGNOSTIC_COMPLETION_SUBSTATE,
      scic_sds_stp_request_started_soft_reset_await_h2d_diagnostic_completion_enter,
      NULL
   },
   {
      SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_D2H_RESPONSE_FRAME_SUBSTATE,
      scic_sds_stp_request_started_soft_reset_await_d2h_response_enter,
      NULL
   }
};

// ---------------------------------------------------------------------------

SCI_STATUS scic_io_request_construct_basic_sata(
   SCI_IO_REQUEST_HANDLE_T  scic_io_request
)
{
   SCI_STATUS           status;
   SCIC_SDS_REQUEST_T * request = (SCIC_SDS_REQUEST_T *)scic_io_request;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(scic_io_request),
      SCIC_LOG_OBJECT_STP_IO_REQUEST,
      "scic_io_request_construct_basic_sata(0x%x) enter\n",
      scic_io_request
   ));

   status = scic_sds_io_request_construct_sata(
               request,
               scic_cb_request_get_sat_protocol(request->user_request),
               scic_cb_io_request_get_transfer_length(request->user_request),
               scic_cb_io_request_get_data_direction(request->user_request),
               scic_cb_io_request_do_copy_rx_frames(request->user_request),
               TRUE
            );

   return status;
}

// ---------------------------------------------------------------------------

SCI_STATUS scic_io_request_construct_advanced_sata(
   SCI_IO_REQUEST_HANDLE_T     scic_io_request,
   SCIC_IO_SATA_PARAMETERS_T * io_parameters
)
{
   SCI_STATUS           status;
   SCIC_SDS_REQUEST_T * request = (SCIC_SDS_REQUEST_T *)scic_io_request;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(scic_io_request),
      SCIC_LOG_OBJECT_STP_IO_REQUEST,
      "scic_io_request_construct_basic_sata(0x%x) enter\n",
      scic_io_request
   ));

   status = scic_sds_io_request_construct_sata(
               request,
               scic_cb_request_get_sat_protocol(request->user_request),
               scic_sds_request_get_sgl_element_pair(request, 0)->A.length,
               scic_cb_io_request_get_data_direction(request->user_request),
               scic_cb_io_request_do_copy_rx_frames(request->user_request),
               io_parameters->do_translate_sgl
            );

   return status;
}

