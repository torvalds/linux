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

#if !defined(DISABLE_ATAPI)

#include <dev/isci/scil/scic_sds_stp_packet_request.h>
#include <dev/isci/scil/scic_sds_logger.h>
#include <dev/isci/scil/scic_sds_controller.h>
#include <dev/isci/scil/scic_sds_remote_device.h>
#include <dev/isci/scil/scic_remote_device.h>
#include <dev/isci/scil/sci_util.h>
#include <dev/isci/scil/intel_sas.h>
#include <dev/isci/scil/intel_ata.h>
#include <dev/isci/scil/intel_sata.h>
#include <dev/isci/scil/scic_user_callback.h>
#include <dev/isci/sci_environment.h>
#include <dev/isci/scil/intel_sat.h>
#include <dev/isci/scil/scic_sds_request.h>
#include <dev/isci/scil/scic_controller.h>
#include <dev/isci/scil/scu_completion_codes.h>
#include <dev/isci/scil/scu_task_context.h>
#include <dev/isci/scil/scic_sds_stp_packet_request.h>
#include <dev/isci/scil/sci_base_state.h>

/**
 * @brief This method will fill in the SCU Task Context for a PACKET fis. And
 *           construct the request STARTED sub-state machine for Packet Protocol
 *           IO.
 *
 * @param[in] this_request This parameter specifies the stp packet request object
 *            being constructed.
 *
 * @return none
 */
SCI_STATUS scic_sds_stp_packet_request_construct(
   SCIC_SDS_REQUEST_T *this_request
)
{
   SATA_FIS_REG_H2D_T * h2d_fis =
      scic_stp_io_request_get_h2d_reg_address(
         this_request
      );

   // Work around, we currently only support PACKET DMA protocol, so we
   // need to make change to Packet Fis features field.
   h2d_fis->features = h2d_fis->features | ATA_PACKET_FEATURE_DMA;

   scic_sds_stp_non_ncq_request_construct(this_request);

   // Build the Packet Fis task context structure
   scu_stp_raw_request_construct_task_context(
      (SCIC_SDS_STP_REQUEST_T*) this_request,
      this_request->task_context_buffer
   );

   sci_base_state_machine_construct(
      &this_request->started_substate_machine,
      &this_request->parent.parent,
      scic_sds_stp_packet_request_started_substate_table,
      SCIC_SDS_STP_PACKET_REQUEST_STARTED_PACKET_PHASE_AWAIT_TC_COMPLETION_SUBSTATE
   );

   return SCI_SUCCESS;
}


/**
 * @brief This method will fill in the SCU Task Context for a Packet request
 *        command phase in PACKET DMA DATA (IN/OUT) type. The following
 *        important settings are utilized:
 *
 *          -# task_type == SCU_TASK_TYPE_PACKET_DMA.  This simply indicates
 *             that a normal request type (i.e. non-raw frame) is being
 *             utilized to perform task management.
 *          -# control_frame == 1.  This ensures that the proper endianness
 *             is set so that the bytes are transmitted in the right order
 *             for a smp request frame.
 *
 * @param[in] this_request This parameter specifies the smp request object
 *            being constructed.
 * @param[in] task_context The task_context to be reconstruct for packet
 *            request command phase.
 * @return none
 */
void scu_stp_packet_request_command_phase_construct_task_context(
   SCIC_SDS_REQUEST_T * this_request,
   SCU_TASK_CONTEXT_T * task_context
)
{
   void * atapi_cdb;
   U32 atapi_cdb_length;
   SCIC_SDS_STP_REQUEST_T * stp_request = (SCIC_SDS_STP_REQUEST_T *)this_request;

   // reference: SSTL 1.13.4.2
   // task_type, sata_direction
   if ( scic_cb_io_request_get_data_direction(this_request->user_request)
           == SCI_IO_REQUEST_DATA_OUT )
   {
      task_context->task_type = SCU_TASK_TYPE_PACKET_DMA_OUT;
      task_context->sata_direction = 0;
   }
   else // todo: for NO_DATA command, we need to send out raw frame.
   {
      task_context->task_type = SCU_TASK_TYPE_PACKET_DMA_IN;
      task_context->sata_direction = 1;
   }

   // sata header
   memset(&(task_context->type.stp), 0, sizeof(struct STP_TASK_CONTEXT));
   task_context->type.stp.fis_type = SATA_FIS_TYPE_DATA;

   // Copy in the command IU with CDB so that the commandIU address doesn't
   // change.
   memset(this_request->command_buffer, 0, sizeof(SATA_FIS_REG_H2D_T));

   atapi_cdb =
      scic_cb_stp_packet_io_request_get_cdb_address(this_request->user_request);

   atapi_cdb_length =
      scic_cb_stp_packet_io_request_get_cdb_length(this_request->user_request);

   memcpy(((U8 *)this_request->command_buffer+sizeof(U32)), atapi_cdb, atapi_cdb_length);

   atapi_cdb_length =
      MAX(atapi_cdb_length, stp_request->type.packet.device_preferred_cdb_length);

   task_context->ssp_command_iu_length =
      ((atapi_cdb_length % 4) == 0) ?
       (atapi_cdb_length / 4) : ((atapi_cdb_length / 4) + 1);

   // task phase is set to TX_CMD
   task_context->task_phase = 0x1;

   // retry counter
   task_context->stp_retry_count = 0;

   if (scic_cb_request_is_initial_construction(this_request->user_request))
   {
      // data transfer size.
      task_context->transfer_length_bytes =
         scic_cb_io_request_get_transfer_length(this_request->user_request);

      // sgls were already built when request was constructed, so don't need to
      //  to do it here
   }
   else
   {
      // data transfer size, need to be 4 bytes aligned.
      task_context->transfer_length_bytes = (SCSI_FIXED_SENSE_DATA_BASE_LENGTH + 2);

      scic_sds_stp_packet_internal_request_sense_build_sgl(this_request);
   }
}

/**
 * @brief This method will fill in the SCU Task Context for a DATA fis
 *        containing CDB in Raw Frame type. The TC for previous Packet
 *        fis was already there, we only need to change the H2D fis content.
 *
 * @param[in] this_request This parameter specifies the smp request object
 *            being constructed.
 * @param[in] task_context The task_context to be reconstruct for packet
 *            request command phase.
 * @return none
 */
void scu_stp_packet_request_command_phase_reconstruct_raw_frame_task_context(
   SCIC_SDS_REQUEST_T * this_request,
   SCU_TASK_CONTEXT_T * task_context
)
{
   void * atapi_cdb =
      scic_cb_stp_packet_io_request_get_cdb_address(this_request->user_request);

   U32 atapi_cdb_length =
      scic_cb_stp_packet_io_request_get_cdb_length(this_request->user_request);

   memset(this_request->command_buffer, 0, sizeof(SATA_FIS_REG_H2D_T));
   memcpy( ((U8 *)this_request->command_buffer+sizeof(U32)), atapi_cdb, atapi_cdb_length);

   memset(&(task_context->type.stp), 0, sizeof(struct STP_TASK_CONTEXT));
   task_context->type.stp.fis_type = SATA_FIS_TYPE_DATA;

   //Note the data send out has to be 4 bytes aligned. Or else out hardware will
   //patch non-zero bytes and cause the target device unhappy.
   task_context->transfer_length_bytes = 12;
}


/*
 *@brief This methods decode the D2H status FIS and retrieve the sense data,
 *          then pass the sense data to user request.
 *
 *@param[in] this_request The request receive D2H status FIS.
 *@param[in] status_fis The D2H status fis to be processed.
 *
 */
SCI_STATUS scic_sds_stp_packet_request_process_status_fis(
   SCIC_SDS_REQUEST_T * this_request,
   SATA_FIS_REG_D2H_T * status_fis
)
{
   SCI_STATUS status = SCI_SUCCESS;

   //TODO: Process the error status fis, retrieve sense data.
   if (status_fis->status & ATA_STATUS_REG_ERROR_BIT)
      status = SCI_FAILURE_IO_RESPONSE_VALID;

   return status;
}

/*
 *@brief This methods builds sgl for internal REQUEST SENSE stp packet
 *          command using this request response buffer, only one sge is
 *          needed.
 *
 *@param[in] this_request The request receive request sense data.
 *
 */
void scic_sds_stp_packet_internal_request_sense_build_sgl(
   SCIC_SDS_REQUEST_T * this_request
)
{
   void                   *sge;
   SCU_SGL_ELEMENT_PAIR_T *scu_sgl_list   = NULL;
   SCU_TASK_CONTEXT_T     *task_context;
   SCI_PHYSICAL_ADDRESS    physical_address;

   SCI_SSP_RESPONSE_IU_T * rsp_iu =
      (SCI_SSP_RESPONSE_IU_T *)this_request->response_buffer;
   sge =  (void*)&rsp_iu->data[0];

   task_context = (SCU_TASK_CONTEXT_T *)this_request->task_context_buffer;
   scu_sgl_list = &task_context->sgl_pair_ab;

   scic_cb_io_request_get_physical_address(
      scic_sds_request_get_controller(this_request),
      this_request,
      ((char *)sge),
      &physical_address
   );

   scu_sgl_list->A.address_upper = sci_cb_physical_address_upper(physical_address);
   scu_sgl_list->A.address_lower = sci_cb_physical_address_lower(physical_address);
   scu_sgl_list->A.length = task_context->transfer_length_bytes;
   scu_sgl_list->A.address_modifier = 0;

   SCU_SGL_ZERO(scu_sgl_list->B);
}

//******************************************************************************
//* STP PACKET REQUEST STATE MACHINES
//******************************************************************************

/**
* @brief This method processes the completions transport layer (TL) status
*        to determine if the Packet FIS was sent successfully. If the Packet
*        FIS was sent successfully, then the state for the Packet request
*        transits to waiting for a PIO SETUP frame.
*
* @param[in] this_request This parameter specifies the request for which
*            the TC completion was received.
* @param[in] completion_code This parameter indicates the completion status
*            information for the TC.
*
* @return Indicate if the tc completion handler was successful.
* @retval SCI_SUCCESS currently this method always returns success.
*/
static
SCI_STATUS scic_sds_stp_packet_request_packet_phase_await_tc_completion_tc_completion_handler(
   SCIC_SDS_REQUEST_T * this_request,
   U32                  completion_code
)
{
   SCI_STATUS status = SCI_SUCCESS;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_request),
      SCIC_LOG_OBJECT_STP_IO_REQUEST,
      "scic_sds_stp_packet_request_packet_phase_await_tc_completion_tc_completion_handler(0x%x, 0x%x) enter\n",
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
         SCIC_SDS_STP_PACKET_REQUEST_STARTED_PACKET_PHASE_AWAIT_PIO_SETUP_SUBSTATE
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
 * @brief This method processes an unsolicited frame while the Packet request
 *        is waiting for a PIO SETUP FIS.  It will release
 *        the unsolicited frame, and transition the request to the
 *        COMMAND_PHASE_AWAIT_TC_COMPLETION_SUBSTATE state.
 *
 * @param[in] this_request This parameter specifies the request for which
 *            the unsolicited frame was received.
 * @param[in] frame_index This parameter indicates the unsolicited frame
 *            index that should contain the response.
 *
 * @return This method returns an indication of whether the pio setup
 *         frame was handled successfully or not.
 * @retval SCI_SUCCESS Currently this value is always returned and indicates
 *         successful processing of the TC response.
 *
 */
static
SCI_STATUS scic_sds_stp_packet_request_packet_phase_await_pio_setup_frame_handler(
   SCIC_SDS_REQUEST_T * request,
   U32                  frame_index
)
{
   SCI_STATUS               status;
   SATA_FIS_HEADER_T      * frame_header;
   U32                    * frame_buffer;
   SCIC_SDS_STP_REQUEST_T * this_request;

   this_request = (SCIC_SDS_STP_REQUEST_T *)request;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_request),
      SCIC_LOG_OBJECT_STP_IO_REQUEST,
      "scic_sds_stp_packet_request_packet_phase_await_pio_setup_frame_handler(0x%x, 0x%x) enter\n",
      this_request, frame_index
   ));

   status = scic_sds_unsolicited_frame_control_get_header(
      &(this_request->parent.owning_controller->uf_control),
      frame_index,
      (void**) &frame_header
   );

   if (status == SCI_SUCCESS)
   {
      ASSERT(frame_header->fis_type == SATA_FIS_TYPE_PIO_SETUP);

      // Get from the frame buffer the PIO Setup Data, although we don't need
      // any info from this pio setup fis.
      scic_sds_unsolicited_frame_control_get_buffer(
         &(this_request->parent.owning_controller->uf_control),
         frame_index,
         (void**) &frame_buffer
      );

      // Get the data from the PIO Setup
      // The SCU Hardware returns first word in the frame_header and the rest
      // of the data is in the frame buffer so we need to back up one dword
      this_request->type.packet.device_preferred_cdb_length =
         (U16)((SATA_FIS_PIO_SETUP_T *)(&frame_buffer[-1]))->transfter_count;

      // Frame has been decoded return it to the controller
      scic_sds_controller_release_frame(
         this_request->parent.owning_controller, frame_index
      );

      sci_base_state_machine_change_state(
         &this_request->parent.started_substate_machine,
         SCIC_SDS_STP_PACKET_REQUEST_STARTED_COMMAND_PHASE_AWAIT_TC_COMPLETION_SUBSTATE
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
 * @brief This method processes the completions transport layer (TL) status
 *        to determine if the PACKET command data FIS was sent successfully.
 *        If successfully, then the state for the packet request
 *        transits to COMPLETE state. If not successfully, the request transits
 *        to COMMAND_PHASE_AWAIT_D2H_FIS_SUBSTATE.
 *
 * @param[in] this_request This parameter specifies the request for which
 *            the TC completion was received.
 * @param[in] completion_code This parameter indicates the completion status
 *            information for the TC.
 *
 * @return Indicate if the tc completion handler was successful.
 * @retval SCI_SUCCESS currently this method always returns success.
 */
static
SCI_STATUS scic_sds_stp_packet_request_command_phase_await_tc_completion_tc_completion_handler(
   SCIC_SDS_REQUEST_T * this_request,
   U32                  completion_code
)
{
   SCI_STATUS status = SCI_SUCCESS;
   U8 sat_packet_protocol = this_request->sat_protocol;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_request),
      SCIC_LOG_OBJECT_STP_IO_REQUEST,
      "scic_sds_stp_packet_request_command_phase_await_tc_completion_tc_completion_handler(0x%x, 0x%x) enter\n",
      this_request, completion_code
   ));

   switch (SCU_GET_COMPLETION_TL_STATUS(completion_code))
   {
   case (SCU_TASK_DONE_GOOD << SCU_COMPLETION_TL_STATUS_SHIFT):
      scic_sds_request_set_status(
         this_request, SCU_TASK_DONE_GOOD, SCI_SUCCESS
      );

      if ( sat_packet_protocol == SAT_PROTOCOL_PACKET_DMA_DATA_IN
          || sat_packet_protocol == SAT_PROTOCOL_PACKET_DMA_DATA_OUT
         )
         sci_base_state_machine_change_state(
            &this_request->parent.state_machine,
            SCI_BASE_REQUEST_STATE_COMPLETED
         );
      else
         sci_base_state_machine_change_state(
            &this_request->started_substate_machine,
            SCIC_SDS_STP_PACKET_REQUEST_STARTED_COMMAND_PHASE_AWAIT_D2H_FIS_SUBSTATE
         );
   break;

   case (SCU_TASK_DONE_UNEXP_FIS << SCU_COMPLETION_TL_STATUS_SHIFT):
      if (scic_io_request_get_number_of_bytes_transferred(this_request) <
          scic_cb_io_request_get_transfer_length(this_request->user_request))
      {
         scic_sds_request_set_status(
            this_request, SCU_TASK_DONE_GOOD, SCI_SUCCESS_IO_DONE_EARLY
         );

         sci_base_state_machine_change_state(
            &this_request->started_substate_machine,
            SCIC_SDS_STP_PACKET_REQUEST_STARTED_COMPLETION_DELAY_SUBSTATE
         );

         //change the device state to ATAPI_ERROR.
         sci_base_state_machine_change_state(
            &this_request->target_device->ready_substate_machine,
            SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_ATAPI_ERROR
         );

         status = this_request->sci_status;
      }
   break;

   case (SCU_TASK_DONE_EXCESS_DATA << SCU_COMPLETION_TL_STATUS_SHIFT):
      //In this case, there is no UF coming after. compelte the IO now.
      scic_sds_request_set_status(
         this_request, SCU_TASK_DONE_GOOD, SCI_SUCCESS
      );

      sci_base_state_machine_change_state(
         &this_request->parent.state_machine,
         SCI_BASE_REQUEST_STATE_COMPLETED
      );

   break;

   default:
      if (this_request->sci_status != SCI_SUCCESS)
      {  //The io status was set already. This means an UF for the status
         //fis was received already.

         //A device suspension event is expected, we need to have the device
         //coming out of suspension, then complete the IO.
         sci_base_state_machine_change_state(
            &this_request->started_substate_machine,
            SCIC_SDS_STP_PACKET_REQUEST_STARTED_COMPLETION_DELAY_SUBSTATE
         );

         //change the device state to ATAPI_ERROR.
         sci_base_state_machine_change_state(
            &this_request->target_device->ready_substate_machine,
            SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_ATAPI_ERROR
         );

         status = this_request->sci_status;
      }
      else
      {  //If receiving any non-sucess TC status, no UF received yet, then an UF for
         //the status fis is coming after.
         scic_sds_request_set_status(
            this_request,
            SCU_TASK_DONE_CHECK_RESPONSE,
            SCI_FAILURE_IO_RESPONSE_VALID
         );

         sci_base_state_machine_change_state(
            &this_request->started_substate_machine,
            SCIC_SDS_STP_PACKET_REQUEST_STARTED_COMMAND_PHASE_AWAIT_D2H_FIS_SUBSTATE
         );
      }
   break;
   }

   return status;
}


/**
* @brief This method processes an unsolicited frame.
*
* @param[in] this_request This parameter specifies the request for which
*            the unsolicited frame was received.
* @param[in] frame_index This parameter indicates the unsolicited frame
*            index that should contain the response.
*
* @return This method returns an indication of whether the UF
*         frame was handled successfully or not.
* @retval SCI_SUCCESS Currently this value is always returned and indicates
*         successful processing of the TC response.
*
*/
static
SCI_STATUS scic_sds_stp_packet_request_command_phase_common_frame_handler(
   SCIC_SDS_REQUEST_T * request,
   U32                  frame_index
)
{
   SCI_STATUS               status;
   SATA_FIS_HEADER_T      * frame_header;
   U32                    * frame_buffer;
   SCIC_SDS_STP_REQUEST_T * this_request;

   this_request = (SCIC_SDS_STP_REQUEST_T *)request;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_request),
      SCIC_LOG_OBJECT_STP_IO_REQUEST,
      "scic_sds_stp_packet_request_command_phase_await_d2h_frame_handler(0x%x, 0x%x) enter\n",
      this_request, frame_index
   ));

   status = scic_sds_unsolicited_frame_control_get_header(
      &(this_request->parent.owning_controller->uf_control),
      frame_index,
      (void**) &frame_header
   );

   if (status == SCI_SUCCESS)
   {
      ASSERT(frame_header->fis_type == SATA_FIS_TYPE_REGD2H);

      // Get from the frame buffer the PIO Setup Data, although we don't need
      // any info from this pio setup fis.
      scic_sds_unsolicited_frame_control_get_buffer(
         &(this_request->parent.owning_controller->uf_control),
         frame_index,
         (void**) &frame_buffer
      );

      scic_sds_controller_copy_sata_response(
         &this_request->d2h_reg_fis, (U32 *)frame_header, frame_buffer
      );

      // Frame has been decoded return it to the controller
      scic_sds_controller_release_frame(
         this_request->parent.owning_controller, frame_index
      );
   }

   return status;
}

/**
* @brief This method processes an unsolicited frame while the packet request is
*        expecting TC completion. It will process the FIS and construct sense
*        data.
*
* @param[in] this_request This parameter specifies the request for which
*            the unsolicited frame was received.
* @param[in] frame_index This parameter indicates the unsolicited frame
*            index that should contain the response.
*
* @return This method returns an indication of whether the UF
*         frame was handled successfully or not.
* @retval SCI_SUCCESS Currently this value is always returned and indicates
*         successful processing of the TC response.
*
*/
static
SCI_STATUS scic_sds_stp_packet_request_command_phase_await_tc_completion_frame_handler(
   SCIC_SDS_REQUEST_T * request,
   U32                  frame_index
)
{
   SCIC_SDS_STP_REQUEST_T * this_request = (SCIC_SDS_STP_REQUEST_T *)request;

   SCI_STATUS status =
      scic_sds_stp_packet_request_command_phase_common_frame_handler(
         request, frame_index);

   if (status == SCI_SUCCESS)
   {
      // The command has completed with error status from target device.
      status = scic_sds_stp_packet_request_process_status_fis(
         request, &this_request->d2h_reg_fis);

      if (status != SCI_SUCCESS)
      {
         scic_sds_request_set_status(
            &this_request->parent,
            SCU_TASK_DONE_CHECK_RESPONSE,
            status
         );
      }
      else
         scic_sds_request_set_status(
            &this_request->parent, SCU_TASK_DONE_GOOD, SCI_SUCCESS
         );
   }

   return status;
}


/**
* @brief This method processes an unsolicited frame while the packet request is
*        expecting TC completion. It will process the FIS and construct sense
*        data.
*
* @param[in] this_request This parameter specifies the request for which
*            the unsolicited frame was received.
* @param[in] frame_index This parameter indicates the unsolicited frame
*            index that should contain the response.
*
* @return This method returns an indication of whether the UF
*         frame was handled successfully or not.
* @retval SCI_SUCCESS Currently this value is always returned and indicates
*         successful processing of the TC response.
*
*/
static
SCI_STATUS scic_sds_stp_packet_request_command_phase_await_d2h_fis_frame_handler(
   SCIC_SDS_REQUEST_T * request,
   U32                  frame_index
)
{
   SCI_STATUS status =
      scic_sds_stp_packet_request_command_phase_common_frame_handler(
         request, frame_index);

   SCIC_SDS_STP_REQUEST_T * this_request = (SCIC_SDS_STP_REQUEST_T *)request;

   if (status == SCI_SUCCESS)
   {
       // The command has completed with error status from target device.
      status = scic_sds_stp_packet_request_process_status_fis(
         request, &this_request->d2h_reg_fis);

      if (status != SCI_SUCCESS)
      {
         scic_sds_request_set_status(
            request,
            SCU_TASK_DONE_CHECK_RESPONSE,
            status
         );
      }
      else
         scic_sds_request_set_status(
            request, SCU_TASK_DONE_GOOD, SCI_SUCCESS
         );

      //Always complete the NON_DATA command right away, no need to delay completion
      //even an error status fis came from target device.
      sci_base_state_machine_change_state(
         &request->parent.state_machine,
         SCI_BASE_REQUEST_STATE_COMPLETED
      );
   }

   return status;
}

static
SCI_STATUS scic_sds_stp_packet_request_started_completion_delay_complete_handler(
   SCI_BASE_REQUEST_T *request
)
{
   SCIC_SDS_REQUEST_T * this_request = (SCIC_SDS_REQUEST_T *)request;

   sci_base_state_machine_change_state(
      &this_request->parent.state_machine,
      SCI_BASE_REQUEST_STATE_COMPLETED
   );

   return this_request->sci_status;
}

// ---------------------------------------------------------------------------

SCIC_SDS_IO_REQUEST_STATE_HANDLER_T
scic_sds_stp_packet_request_started_substate_handler_table
[SCIC_SDS_STP_PACKET_REQUEST_STARTED_MAX_SUBSTATES] =
{
   // SCIC_SDS_STP_PACKET_REQUEST_STARTED_PACKET_PHASE_AWAIT_TC_COMPLETION_SUBSTATE
   {
      {
         scic_sds_request_default_start_handler,
         scic_sds_request_started_state_abort_handler,
         scic_sds_request_default_complete_handler,
         scic_sds_request_default_destruct_handler
      },
      scic_sds_stp_packet_request_packet_phase_await_tc_completion_tc_completion_handler,
      scic_sds_request_default_event_handler,
      scic_sds_request_default_frame_handler
   },
   // SCIC_SDS_STP_PACKET_REQUEST_STARTED_PACKET_PHASE_AWAIT_PIO_SETUP_SUBSTATE
   {
      {
         scic_sds_request_default_start_handler,
         scic_sds_request_started_state_abort_handler,
         scic_sds_request_default_complete_handler,
         scic_sds_request_default_destruct_handler
      },
      scic_sds_request_default_tc_completion_handler,
      scic_sds_request_default_event_handler,
      scic_sds_stp_packet_request_packet_phase_await_pio_setup_frame_handler
   },
   // SCIC_SDS_STP_PACKET_REQUEST_STARTED_COMMAND_PHASE_AWAIT_TC_COMPLETION_SUBSTATE
   {
      {
         scic_sds_request_default_start_handler,
         scic_sds_request_started_state_abort_handler,
         scic_sds_request_default_complete_handler,
         scic_sds_request_default_destruct_handler
      },
      scic_sds_stp_packet_request_command_phase_await_tc_completion_tc_completion_handler,
      scic_sds_request_default_event_handler,
      scic_sds_stp_packet_request_command_phase_await_tc_completion_frame_handler
   },
   // SCIC_SDS_STP_PACKET_REQUEST_STARTED_COMMAND_PHASE_AWAIT_D2H_FIS_SUBSTATE
   {
      {
         scic_sds_request_default_start_handler,
         scic_sds_request_started_state_abort_handler,
         scic_sds_request_default_complete_handler,
         scic_sds_request_default_destruct_handler
      },
      scic_sds_request_default_tc_completion_handler,
      scic_sds_request_default_event_handler,
      scic_sds_stp_packet_request_command_phase_await_d2h_fis_frame_handler
   },
   // SCIC_SDS_STP_PACKET_REQUEST_STARTED_COMPLETION_DELAY_SUBSTATE
   {
      {
         scic_sds_request_default_start_handler,
         scic_sds_request_started_state_abort_handler,
         scic_sds_stp_packet_request_started_completion_delay_complete_handler,
         scic_sds_request_default_destruct_handler
      },
      scic_sds_request_default_tc_completion_handler,
      scic_sds_request_default_event_handler,
      scic_sds_request_default_frame_handler
   }
};

/**
 * @file
 *
 * @brief This file contains the Packet IO started substate machine
 *        for the SCIC_SDS_IO_REQUEST object.
 */
static
void scic_sds_stp_packet_request_started_packet_phase_await_tc_completion_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REQUEST_T *this_request = (SCIC_SDS_REQUEST_T *)object;

   SET_STATE_HANDLER(
      this_request,
      scic_sds_stp_packet_request_started_substate_handler_table,
      SCIC_SDS_STP_PACKET_REQUEST_STARTED_PACKET_PHASE_AWAIT_TC_COMPLETION_SUBSTATE
   );

   scic_sds_remote_device_set_working_request(
      this_request->target_device, this_request
   );
}

static
void scic_sds_stp_packet_request_started_packet_phase_await_pio_setup_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REQUEST_T *this_request = (SCIC_SDS_REQUEST_T *)object;

   SET_STATE_HANDLER(
      this_request,
      scic_sds_stp_packet_request_started_substate_handler_table,
      SCIC_SDS_STP_PACKET_REQUEST_STARTED_PACKET_PHASE_AWAIT_PIO_SETUP_SUBSTATE
   );
}

static
void scic_sds_stp_packet_request_started_command_phase_await_tc_completion_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REQUEST_T *this_request = (SCIC_SDS_REQUEST_T *)object;
   U8 sat_packet_protocol = this_request->sat_protocol;

   SCU_TASK_CONTEXT_T *task_context;
   SCI_STATUS status;

   // Recycle the TC and reconstruct it for sending out data fis containing
   // CDB.
   task_context = scic_sds_controller_get_task_context_buffer(
                        this_request->owning_controller, this_request->io_tag);

   if (sat_packet_protocol == SAT_PROTOCOL_PACKET_NON_DATA)
      scu_stp_packet_request_command_phase_reconstruct_raw_frame_task_context(
         this_request, task_context);
   else
      scu_stp_packet_request_command_phase_construct_task_context(
         this_request, task_context);

   // send the new TC out.
   status = this_request->owning_controller->state_handlers->parent.continue_io_handler(
      &this_request->owning_controller->parent,
      &this_request->target_device->parent,
      &this_request->parent
   );

   if (status == SCI_SUCCESS)
      SET_STATE_HANDLER(
         this_request,
         scic_sds_stp_packet_request_started_substate_handler_table,
         SCIC_SDS_STP_PACKET_REQUEST_STARTED_COMMAND_PHASE_AWAIT_TC_COMPLETION_SUBSTATE
      );
}

static
void scic_sds_stp_packet_request_started_command_phase_await_d2h_fis_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REQUEST_T *this_request = (SCIC_SDS_REQUEST_T *)object;

   SET_STATE_HANDLER(
      this_request,
      scic_sds_stp_packet_request_started_substate_handler_table,
      SCIC_SDS_STP_PACKET_REQUEST_STARTED_COMMAND_PHASE_AWAIT_D2H_FIS_SUBSTATE
   );
}

static
void scic_sds_stp_packet_request_started_completion_delay_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REQUEST_T *this_request = (SCIC_SDS_REQUEST_T *)object;

   SET_STATE_HANDLER(
      this_request,
      scic_sds_stp_packet_request_started_substate_handler_table,
      SCIC_SDS_STP_PACKET_REQUEST_STARTED_COMPLETION_DELAY_SUBSTATE
   );
}


// ---------------------------------------------------------------------------
SCI_BASE_STATE_T
   scic_sds_stp_packet_request_started_substate_table
      [SCIC_SDS_STP_PACKET_REQUEST_STARTED_MAX_SUBSTATES] =
{
   {
      SCIC_SDS_STP_PACKET_REQUEST_STARTED_PACKET_PHASE_AWAIT_TC_COMPLETION_SUBSTATE,
      scic_sds_stp_packet_request_started_packet_phase_await_tc_completion_enter,
      NULL
   },
   {
      SCIC_SDS_STP_PACKET_REQUEST_STARTED_PACKET_PHASE_AWAIT_PIO_SETUP_SUBSTATE,
      scic_sds_stp_packet_request_started_packet_phase_await_pio_setup_enter,
      NULL
   },
   {
      SCIC_SDS_STP_PACKET_REQUEST_STARTED_COMMAND_PHASE_AWAIT_TC_COMPLETION_SUBSTATE,
      scic_sds_stp_packet_request_started_command_phase_await_tc_completion_enter,
      NULL
   },
   {
      SCIC_SDS_STP_PACKET_REQUEST_STARTED_COMMAND_PHASE_AWAIT_D2H_FIS_SUBSTATE,
      scic_sds_stp_packet_request_started_command_phase_await_d2h_fis_enter,
      NULL
   },
   {
      SCIC_SDS_STP_PACKET_REQUEST_STARTED_COMPLETION_DELAY_SUBSTATE,
      scic_sds_stp_packet_request_started_completion_delay_enter,
      NULL
   }
};

#endif //#if !defined(DISABLE_ATAPI)

