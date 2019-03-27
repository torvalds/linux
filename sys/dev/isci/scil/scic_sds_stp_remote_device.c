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
 * @brief This file contains the methods and state machines for SATA/STP
 *        remote devices.
 */

#include <dev/isci/scil/intel_sat.h>
#include <dev/isci/scil/intel_ata.h>
#include <dev/isci/scil/intel_sata.h>
#include <dev/isci/scil/scic_remote_device.h>
#include <dev/isci/scil/scic_user_callback.h>
#include <dev/isci/scil/scic_sds_logger.h>
#include <dev/isci/scil/scic_sds_controller.h>
#include <dev/isci/scil/scic_sds_port.h>
#include <dev/isci/scil/scic_sds_remote_device.h>
#include <dev/isci/scil/scic_sds_request.h>
#include <dev/isci/scil/scu_event_codes.h>
#include <dev/isci/scil/scu_completion_codes.h>
#include <dev/isci/scil/sci_base_state.h>

/**
 * This method will perform the STP request completion processing common
 * to IO requests and task requests of all types
 *
 * @param[in] device This parameter specifies the device for which the
 *            request is being completed.
 * @param[in] request This parameter specifies the request being completed.
 *
 * @return This method returns an indication as to whether the request
 *         processing completed successfully.
 */
static
SCI_STATUS scic_sds_stp_remote_device_complete_request(
   SCI_BASE_REMOTE_DEVICE_T * device,
   SCI_BASE_REQUEST_T       * request
)
{
   SCIC_SDS_REMOTE_DEVICE_T * this_device = (SCIC_SDS_REMOTE_DEVICE_T *)device;
   SCIC_SDS_REQUEST_T       * the_request = (SCIC_SDS_REQUEST_T *)request;
   SCI_STATUS                 status;

   status = scic_sds_io_request_complete(the_request);

   if (status == SCI_SUCCESS)
   {
      status = scic_sds_port_complete_io(
                  this_device->owning_port, this_device, the_request
               );

      if (status == SCI_SUCCESS)
      {
         scic_sds_remote_device_decrement_request_count(this_device);
         if (the_request->sci_status == SCI_FAILURE_REMOTE_DEVICE_RESET_REQUIRED)
         {
            //This request causes hardware error, device needs to be Lun Reset.
            //So here we force the state machine to IDLE state so the rest IOs
            //can reach RNC state handler, these IOs will be completed by RNC with
            //status of "DEVICE_RESET_REQUIRED", instead of "INVALID STATE".
            sci_base_state_machine_change_state(
               &this_device->ready_substate_machine,
               SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_AWAIT_RESET
            );
         }
         else if (scic_sds_remote_device_get_request_count(this_device) == 0)
         {
            sci_base_state_machine_change_state(
               &this_device->ready_substate_machine,
               SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_IDLE
            );
         }
      }
   }

   if (status != SCI_SUCCESS)
   {
      SCIC_LOG_ERROR((
         sci_base_object_get_logger(this_device),
         SCIC_LOG_OBJECT_STP_REMOTE_TARGET,
         "Port:0x%x Device:0x%x Request:0x%x Status:0x%x could not complete\n",
         this_device->owning_port, this_device, the_request, status
      ));
   }

   return status;
}

//*****************************************************************************
//*  STP REMOTE DEVICE READY COMMON SUBSTATE HANDLERS
//*****************************************************************************

/**
 * This is the READY NCQ substate handler to start task management request. In this
 * routine, we suspend and resume the RNC.
 *
 * @param[in] device The target device a task management request towards to.
 * @param[in] request The task request.
 *
 * @return SCI_STATUS Always return SCI_FAILURE_RESET_DEVICE_PARTIAL_SUCCESS status
 *     to let controller_start_task_handler know that the controller can't post TC for
 *     task request yet, instead, when RNC gets resumed, a controller_continue_task
 *     callback will be called.
 */
static
SCI_STATUS scic_sds_stp_remote_device_ready_substate_start_request_handler(
   SCI_BASE_REMOTE_DEVICE_T * device,
   SCI_BASE_REQUEST_T       * request
)
{
   SCI_STATUS status;
   SCIC_SDS_REMOTE_DEVICE_T * this_device  = (SCIC_SDS_REMOTE_DEVICE_T *)device;
   SCIC_SDS_REQUEST_T       * this_request = (SCIC_SDS_REQUEST_T       *)request;

   // Will the port allow the io request to start?
   status = this_device->owning_port->state_handlers->start_io_handler(
      this_device->owning_port,
      this_device,
      this_request
   );

   if (SCI_SUCCESS == status)
   {
      status =
         scic_sds_remote_node_context_start_task(this_device->rnc, this_request);

      if (SCI_SUCCESS == status)
      {
         status = this_request->state_handlers->parent.start_handler(request);
      }

      if (status == SCI_SUCCESS)
      {
         /// @note If the remote device state is not IDLE this will replace
         ///       the request that probably resulted in the task management
         ///       request.
         this_device->working_request = this_request;

         sci_base_state_machine_change_state(
            &this_device->ready_substate_machine,
            SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_CMD
         );

         //The remote node context must cleanup the TCi to NCQ mapping table.
         //The only way to do this correctly is to either write to the TLCR
         //register or to invalidate and repost the RNC. In either case the
         //remote node context state machine will take the correct action when
         //the remote node context is suspended and later resumed.
         scic_sds_remote_node_context_suspend(
            this_device->rnc, SCI_SOFTWARE_SUSPENSION, NULL, NULL);

         scic_sds_remote_node_context_resume(
            this_device->rnc,
            (SCIC_SDS_REMOTE_NODE_CONTEXT_CALLBACK)
                scic_sds_remote_device_continue_request,
            this_device);
      }

      scic_sds_remote_device_start_request(this_device,this_request,status);

      //We need to let the controller start request handler know that it can't
      //post TC yet. We will provide a callback function to post TC when RNC gets
      //resumed.
      return SCI_FAILURE_RESET_DEVICE_PARTIAL_SUCCESS;
   }

   return status;
}

//*****************************************************************************
//*  STP REMOTE DEVICE READY IDLE SUBSTATE HANDLERS
//*****************************************************************************

/**
 * This method will handle the start io operation for a sata device that is in
 * the command idle state.
 *    - Evalute the type of IO request to be started
 *    - If its an NCQ request change to NCQ substate
 *    - If its any other command change to the CMD substate
 *
 * @note If this is a softreset we may want to have a different substate.
 *
 * @param [in] device
 * @param [in] request
 *
 * @return SCI_STATUS
 */
static
SCI_STATUS scic_sds_stp_remote_device_ready_idle_substate_start_io_handler(
   SCI_BASE_REMOTE_DEVICE_T * device,
   SCI_BASE_REQUEST_T       * request
)
{
   SCI_STATUS status;
   SCIC_SDS_REMOTE_DEVICE_T * this_device = (SCIC_SDS_REMOTE_DEVICE_T *)device;
   SCIC_SDS_REQUEST_T       * io_request  = (SCIC_SDS_REQUEST_T       *)request;


   // Will the port allow the io request to start?
   status = this_device->owning_port->state_handlers->start_io_handler(
      this_device->owning_port,
      this_device,
      io_request
   );

   if (status == SCI_SUCCESS)
   {
      status =
         scic_sds_remote_node_context_start_io(this_device->rnc, io_request);

      if (status == SCI_SUCCESS)
      {
         status = io_request->state_handlers->parent.start_handler(request);
      }

      if (status == SCI_SUCCESS)
      {
         if (io_request->sat_protocol == SAT_PROTOCOL_FPDMA)
         {
            sci_base_state_machine_change_state(
               &this_device->ready_substate_machine,
               SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_NCQ
            );
         }
         else
         {
            this_device->working_request = io_request;

            sci_base_state_machine_change_state(
               &this_device->ready_substate_machine,
               SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_CMD
            );
         }
      }

      scic_sds_remote_device_start_request(this_device, io_request, status);
   }

   return status;
}


/**
 * This method will handle the event for a sata device that is in
 * the idle state. We pick up suspension events to handle specifically
 * to this state. We resume the RNC right away.
 *
 * @param [in] device The device received event.
 * @param [in] event_code The event code.
 *
 * @return SCI_STATUS
 */
static
SCI_STATUS scic_sds_stp_remote_device_ready_idle_substate_event_handler(
   SCIC_SDS_REMOTE_DEVICE_T * this_device,
   U32                        event_code
)
{
   SCI_STATUS status;

   status = scic_sds_remote_device_general_event_handler(this_device, event_code);

   if (status == SCI_SUCCESS)
   {
      if ((scu_get_event_type(event_code) == SCU_EVENT_TYPE_RNC_SUSPEND_TX
          || scu_get_event_type(event_code) == SCU_EVENT_TYPE_RNC_SUSPEND_TX_RX)
          && (this_device->rnc->destination_state != SCIC_SDS_REMOTE_NODE_DESTINATION_STATE_READY))
      {
         status = scic_sds_remote_node_context_resume(
                  this_device->rnc, NULL, NULL);
      }
   }

   return status;
}


//*****************************************************************************
//*  STP REMOTE DEVICE READY NCQ SUBSTATE HANDLERS
//*****************************************************************************

/**
 *
 */
static
SCI_STATUS scic_sds_stp_remote_device_ready_ncq_substate_start_io_handler(
   SCI_BASE_REMOTE_DEVICE_T * device,
   SCI_BASE_REQUEST_T       * request
)
{
   SCI_STATUS status;
   SCIC_SDS_REMOTE_DEVICE_T * this_device = (SCIC_SDS_REMOTE_DEVICE_T *)device;
   SCIC_SDS_REQUEST_T       * io_request  = (SCIC_SDS_REQUEST_T       *)request;

   if (io_request->sat_protocol == SAT_PROTOCOL_FPDMA)
   {
      status = this_device->owning_port->state_handlers->start_io_handler(
         this_device->owning_port,
         this_device,
         io_request
      );

      if (status == SCI_SUCCESS)
      {
         status = scic_sds_remote_node_context_start_io(this_device->rnc, io_request);

         if (status == SCI_SUCCESS)
         {
            status = io_request->state_handlers->parent.start_handler(request);
         }

         scic_sds_remote_device_start_request(this_device, io_request, status);
      }
   }
   else
   {
      status = SCI_FAILURE_INVALID_STATE;
   }

   return status;
}

/**
 *  This method will handle events received while the STP device is in the
 *  ready command substate.
 *
 *  @param [in] this_device This is the device object that is receiving the
 *         event.
 *  @param [in] event_code The event code to process.
 *
 *  @return SCI_STATUS
 */
static
SCI_STATUS scic_sds_stp_remote_device_ready_ncq_substate_event_handler(
   SCIC_SDS_REMOTE_DEVICE_T * this_device,
   U32                        event_code
)
{
   SCI_STATUS status;

   status = scic_sds_remote_device_general_event_handler(this_device, event_code);

   switch (scu_get_event_code(event_code))
   {
   case SCU_EVENT_TL_RNC_SUSPEND_TX:
   case SCU_EVENT_TL_RNC_SUSPEND_TX_RX:
      /// @todo We need to decode and understand why the hardware suspended the device.
      ///       The suspension reason was probably due to an SDB error FIS received.
      break;

   case SCU_EVENT_TL_RNC_SUSPEND_TX_DONE_DATA_LEN_ERR:
   case SCU_EVENT_TL_RNC_SUSPEND_TX_DONE_OFFSET_ERR:
   case SCU_EVENT_TL_RNC_SUSPEND_TX_DONE_DMASETUP_DIERR:
   case SCU_EVENT_TL_RNC_SUSPEND_TX_DONE_XFERCNT_ERR:
   case SCU_EVENT_TL_RNC_SUSPEND_TX_RX_DONE_PLD_LEN_ERR:
      this_device->not_ready_reason =
         SCIC_REMOTE_DEVICE_NOT_READY_SATA_SDB_ERROR_FIS_RECEIVED;

      sci_base_state_machine_change_state(
         &this_device->ready_substate_machine,
         SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_NCQ_ERROR
      );

      // We have a notification that the driver requested a suspend operation
      // this should not happen.
      SCIC_LOG_WARNING((
         sci_base_object_get_logger(this_device),
         SCIC_LOG_OBJECT_STP_REMOTE_TARGET,
         "SCIC Remote device 0x%x received driver suspend event %x while in ncq ready substate %d\n",
         this_device, event_code, sci_base_state_machine_get_state(&this_device->ready_substate_machine)
      ));

      // Since we didn't expect to get here start the device again.
      status = scic_sds_remote_device_resume(this_device);
      break;

   case SCU_EVENT_POST_RCN_RELEASE:
      /// @todo Do we need to store the suspend state on the device?
      SCIC_LOG_INFO((
         sci_base_object_get_logger(this_device),
         SCIC_LOG_OBJECT_STP_REMOTE_TARGET,
         "SCIC Remote device 0x%x received driver release event %x while in the ready substate %d\n",
         this_device, event_code, sci_base_state_machine_get_state(&this_device->ready_substate_machine)
      ));
      break;

   default:
      // Some other event just log it and continue
      SCIC_LOG_WARNING((
         sci_base_object_get_logger(this_device),
         SCIC_LOG_OBJECT_STP_REMOTE_TARGET,
         "SCIC Remote device 0x%x received driver unexpected event %x while in the ready substate %d\n",
         this_device, event_code, sci_base_state_machine_get_state(&this_device->ready_substate_machine)
      ));

      status = SCI_FAILURE_INVALID_STATE;
      break;
   }

   return status;
}

/**
 *
 *
 * @param[in] this_device
 * @param[in] frame_index
 *
 * @return SCI_STATUS
 */
static
SCI_STATUS scic_sds_stp_remote_device_ready_ncq_substate_frame_handler(
   SCIC_SDS_REMOTE_DEVICE_T * this_device,
   U32                        frame_index
)
{
   SCI_STATUS           status;
   SATA_FIS_HEADER_T  * frame_header;

   status = scic_sds_unsolicited_frame_control_get_header(
      &(scic_sds_remote_device_get_controller(this_device)->uf_control),
      frame_index,
      (void **)&frame_header
   );

   if (status == SCI_SUCCESS)
   {
      if (
            (frame_header->fis_type == SATA_FIS_TYPE_SETDEVBITS)
         && (frame_header->status & ATA_STATUS_REG_ERROR_BIT)
         )
      {
         this_device->not_ready_reason =
            SCIC_REMOTE_DEVICE_NOT_READY_SATA_SDB_ERROR_FIS_RECEIVED;

         /** @todo Check sactive and complete associated IO if any. */

         sci_base_state_machine_change_state(
            &this_device->ready_substate_machine,
            SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_NCQ_ERROR
         );
      }
      else if (
            (frame_header->fis_type == SATA_FIS_TYPE_REGD2H)
         && (frame_header->status & ATA_STATUS_REG_ERROR_BIT)
         )
      {
         // Some devices return D2H FIS when an NCQ error is detected.
         // Treat this like an SDB error FIS ready reason.
         this_device->not_ready_reason =
            SCIC_REMOTE_DEVICE_NOT_READY_SATA_SDB_ERROR_FIS_RECEIVED;

         sci_base_state_machine_change_state(
            &this_device->ready_substate_machine,
            SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_NCQ_ERROR
         );
      }
      else
      {
         status = SCI_FAILURE;
      }

      scic_sds_controller_release_frame(
         scic_sds_remote_device_get_controller(this_device), frame_index
      );
   }

   return status;
}

//*****************************************************************************
//*  STP REMOTE DEVICE READY CMD SUBSTATE HANDLERS
//*****************************************************************************

/**
 * This device is already handling a command it can not accept new commands
 * until this one is complete.
 *
 * @param[in] device
 * @param[in] request
 *
 * @return SCI_STATUS
 */
static
SCI_STATUS scic_sds_stp_remote_device_ready_cmd_substate_start_io_handler(
   SCI_BASE_REMOTE_DEVICE_T * device,
   SCI_BASE_REQUEST_T       * request
)
{
   return SCI_FAILURE_INVALID_STATE;
}

static
SCI_STATUS scic_sds_stp_remote_device_ready_cmd_substate_suspend_handler(
   SCIC_SDS_REMOTE_DEVICE_T * this_device,
   U32                        suspend_type
)
{
   SCI_STATUS status;

   status = scic_sds_remote_node_context_suspend(
      this_device->rnc, suspend_type, NULL, NULL
   );

   return status;
}

/**
 *
 *
 * @param[in] this_device
 * @param[in] frame_index
 *
 * @return SCI_STATUS
 */
static
SCI_STATUS scic_sds_stp_remote_device_ready_cmd_substate_frame_handler(
   SCIC_SDS_REMOTE_DEVICE_T * this_device,
   U32                        frame_index
)
{
   SCI_STATUS status;

   /// The device doe not process any UF received from the hardware while
   /// in this state.  All unsolicited frames are forwarded to the io request
   /// object.
   status = scic_sds_io_request_frame_handler(
      this_device->working_request,
      frame_index
   );

   return status;
}


//*****************************************************************************
//*  STP REMOTE DEVICE READY NCQ SUBSTATE HANDLERS
//*****************************************************************************


//*****************************************************************************
//*  STP REMOTE DEVICE READY AWAIT RESET SUBSTATE HANDLERS
//*****************************************************************************
static
SCI_STATUS scic_sds_stp_remote_device_ready_await_reset_substate_start_io_handler(
   SCI_BASE_REMOTE_DEVICE_T * device,
   SCI_BASE_REQUEST_T       * request
)
{
   return SCI_FAILURE_REMOTE_DEVICE_RESET_REQUIRED;
}



/**
 * This method will perform the STP request (both io or task) completion
 * processing for await reset state.
 *
 * @param[in] device This parameter specifies the device for which the
 *            request is being completed.
 * @param[in] request This parameter specifies the request being completed.
 *
 * @return This method returns an indication as to whether the request
 *         processing completed successfully.
 */
static
SCI_STATUS scic_sds_stp_remote_device_ready_await_reset_substate_complete_request_handler(
   SCI_BASE_REMOTE_DEVICE_T * device,
   SCI_BASE_REQUEST_T       * request
)
{
   SCIC_SDS_REMOTE_DEVICE_T * this_device = (SCIC_SDS_REMOTE_DEVICE_T *)device;
   SCIC_SDS_REQUEST_T       * the_request = (SCIC_SDS_REQUEST_T *)request;
   SCI_STATUS                 status;

   status = scic_sds_io_request_complete(the_request);

   if (status == SCI_SUCCESS)
   {
      status = scic_sds_port_complete_io(
                  this_device->owning_port, this_device, the_request
               );

      if (status == SCI_SUCCESS)
         scic_sds_remote_device_decrement_request_count(this_device);
   }

   if (status != SCI_SUCCESS)
   {
      SCIC_LOG_ERROR((
         sci_base_object_get_logger(this_device),
         SCIC_LOG_OBJECT_STP_REMOTE_TARGET,
         "Port:0x%x Device:0x%x Request:0x%x Status:0x%x could not complete\n",
         this_device->owning_port, this_device, the_request, status
      ));
   }

   return status;
}

#if !defined(DISABLE_ATAPI)
//*****************************************************************************
//*  STP REMOTE DEVICE READY ATAPI ERROR SUBSTATE HANDLERS
//*****************************************************************************

/**
 * This method will handle the event for a ATAPI device that is in
 * the ATAPI ERROR state. We pick up suspension events to handle specifically
 * to this state. We resume the RNC right away. We then complete the outstanding
 * IO to this device.
 *
 * @param [in] device The device received event.
 * @param [in] event_code The event code.
 *
 * @return SCI_STATUS
 */
static
SCI_STATUS scic_sds_stp_remote_device_ready_atapi_error_substate_event_handler(
   SCIC_SDS_REMOTE_DEVICE_T * this_device,
   U32                        event_code
)
{
   SCI_STATUS status;

   status = scic_sds_remote_device_general_event_handler(this_device, event_code);

   if (status == SCI_SUCCESS)
   {
      if (scu_get_event_type(event_code) == SCU_EVENT_TYPE_RNC_SUSPEND_TX
          || scu_get_event_type(event_code) == SCU_EVENT_TYPE_RNC_SUSPEND_TX_RX)
      {
         status = scic_sds_remote_node_context_resume(
                     this_device->rnc,
                     (SCIC_SDS_REMOTE_NODE_CONTEXT_CALLBACK)
                        this_device->working_request->state_handlers->parent.complete_handler,
                     (void *)this_device->working_request
                  );
      }
   }

   return status;
}
#endif // !defined(DISABLE_ATAPI)

// ---------------------------------------------------------------------------

SCIC_SDS_REMOTE_DEVICE_STATE_HANDLER_T
   scic_sds_stp_remote_device_ready_substate_handler_table[
                              SCIC_SDS_STP_REMOTE_DEVICE_READY_MAX_SUBSTATES] =
{
   // SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_IDLE
   {
      {
         scic_sds_remote_device_default_start_handler,
         scic_sds_remote_device_ready_state_stop_handler,
         scic_sds_remote_device_default_fail_handler,
         scic_sds_remote_device_default_destruct_handler,
         scic_sds_remote_device_ready_state_reset_handler,
         scic_sds_remote_device_default_reset_complete_handler,
         scic_sds_stp_remote_device_ready_idle_substate_start_io_handler,
         scic_sds_remote_device_default_complete_request_handler,
         scic_sds_remote_device_default_continue_request_handler,
         scic_sds_stp_remote_device_ready_substate_start_request_handler,
         scic_sds_remote_device_default_complete_request_handler
      },
      scic_sds_remote_device_default_suspend_handler,
      scic_sds_remote_device_default_resume_handler,
      scic_sds_stp_remote_device_ready_idle_substate_event_handler,
      scic_sds_remote_device_default_frame_handler
   },
   // SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_CMD
   {
      {
         scic_sds_remote_device_default_start_handler,
         scic_sds_remote_device_ready_state_stop_handler,
         scic_sds_remote_device_default_fail_handler,
         scic_sds_remote_device_default_destruct_handler,
         scic_sds_remote_device_ready_state_reset_handler,
         scic_sds_remote_device_default_reset_complete_handler,
         scic_sds_stp_remote_device_ready_cmd_substate_start_io_handler,
         scic_sds_stp_remote_device_complete_request,
         scic_sds_remote_device_default_continue_request_handler,
         scic_sds_stp_remote_device_ready_substate_start_request_handler,
         scic_sds_stp_remote_device_complete_request,
      },
      scic_sds_stp_remote_device_ready_cmd_substate_suspend_handler,
      scic_sds_remote_device_default_resume_handler,
      scic_sds_remote_device_general_event_handler,
      scic_sds_stp_remote_device_ready_cmd_substate_frame_handler
   },
   // SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_NCQ
   {
      {
         scic_sds_remote_device_default_start_handler,
         scic_sds_remote_device_ready_state_stop_handler,
         scic_sds_remote_device_default_fail_handler,
         scic_sds_remote_device_default_destruct_handler,
         scic_sds_remote_device_ready_state_reset_handler,
         scic_sds_remote_device_default_reset_complete_handler,
         scic_sds_stp_remote_device_ready_ncq_substate_start_io_handler,
         scic_sds_stp_remote_device_complete_request,
         scic_sds_remote_device_default_continue_request_handler,
         scic_sds_stp_remote_device_ready_substate_start_request_handler,
         scic_sds_stp_remote_device_complete_request
      },
      scic_sds_remote_device_default_suspend_handler,
      scic_sds_remote_device_default_resume_handler,
      scic_sds_stp_remote_device_ready_ncq_substate_event_handler,
      scic_sds_stp_remote_device_ready_ncq_substate_frame_handler
   },
   // SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_NCQ_ERROR
   {
      {
         scic_sds_remote_device_default_start_handler,
         scic_sds_remote_device_ready_state_stop_handler,
         scic_sds_remote_device_default_fail_handler,
         scic_sds_remote_device_default_destruct_handler,
         scic_sds_remote_device_ready_state_reset_handler,
         scic_sds_remote_device_default_reset_complete_handler,
         scic_sds_remote_device_default_start_request_handler,
         scic_sds_stp_remote_device_complete_request,
         scic_sds_remote_device_default_continue_request_handler,
         scic_sds_stp_remote_device_ready_substate_start_request_handler,
         scic_sds_stp_remote_device_complete_request
      },
      scic_sds_remote_device_default_suspend_handler,
      scic_sds_remote_device_default_resume_handler,
      scic_sds_remote_device_general_event_handler,
      scic_sds_remote_device_general_frame_handler
   },
#if !defined(DISABLE_ATAPI)
   // SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_ATAPI_ERROR
   {
      {
         scic_sds_remote_device_default_start_handler,
         scic_sds_remote_device_ready_state_stop_handler,
         scic_sds_remote_device_default_fail_handler,
         scic_sds_remote_device_default_destruct_handler,
         scic_sds_remote_device_ready_state_reset_handler,
         scic_sds_remote_device_default_reset_complete_handler,
         scic_sds_remote_device_default_start_request_handler,
         scic_sds_stp_remote_device_complete_request,
         scic_sds_remote_device_default_continue_request_handler,
         scic_sds_stp_remote_device_ready_substate_start_request_handler,
         scic_sds_stp_remote_device_complete_request
      },
      scic_sds_remote_device_default_suspend_handler,
      scic_sds_remote_device_default_resume_handler,
      scic_sds_stp_remote_device_ready_atapi_error_substate_event_handler,
      scic_sds_remote_device_general_frame_handler
   },
#endif
   // SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_AWAIT_RESET
   {
      {
         scic_sds_remote_device_default_start_handler,
         scic_sds_remote_device_ready_state_stop_handler,
         scic_sds_remote_device_default_fail_handler,
         scic_sds_remote_device_default_destruct_handler,
         scic_sds_remote_device_ready_state_reset_handler,
         scic_sds_remote_device_default_reset_complete_handler,
         scic_sds_stp_remote_device_ready_await_reset_substate_start_io_handler,
         scic_sds_stp_remote_device_ready_await_reset_substate_complete_request_handler,
         scic_sds_remote_device_default_continue_request_handler,
         scic_sds_stp_remote_device_ready_substate_start_request_handler,
         scic_sds_stp_remote_device_complete_request
      },
      scic_sds_remote_device_default_suspend_handler,
      scic_sds_remote_device_default_resume_handler,
      scic_sds_remote_device_general_event_handler,
      scic_sds_remote_device_general_frame_handler
   }
};

//*****************************************************************************
//*  STP REMOTE DEVICE READY SUBSTATE PRIVATE METHODS
//*****************************************************************************

static
void scic_sds_stp_remote_device_ready_idle_substate_resume_complete_handler(
   void * user_cookie
)
{
   SCIC_SDS_REMOTE_DEVICE_T * this_device;
   this_device = (SCIC_SDS_REMOTE_DEVICE_T *)user_cookie;

   // For NCQ operation we do not issue a
   // scic_cb_remote_device_not_ready().  As a result, avoid sending
   // the ready notification.
   if (this_device->ready_substate_machine.previous_state_id
       != SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_NCQ)
   {
      scic_cb_remote_device_ready(
         scic_sds_remote_device_get_controller(this_device), this_device
      );
   }
}

//*****************************************************************************
//*  STP REMOTE DEVICE READY IDLE SUBSTATE
//*****************************************************************************

/**
 *
 * @param[in] device This is the SCI base object which is cast into a
 *       SCIC_SDS_REMOTE_DEVICE object.
 *
 * @return none
 */
static
void scic_sds_stp_remote_device_ready_idle_substate_enter(
   SCI_BASE_OBJECT_T * device
)
{
   SCIC_SDS_REMOTE_DEVICE_T * this_device;

   this_device = (SCIC_SDS_REMOTE_DEVICE_T *)device;

   SET_STATE_HANDLER(
      this_device,
      scic_sds_stp_remote_device_ready_substate_handler_table,
      SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_IDLE
   );

   this_device->working_request = NULL;

   if (scic_sds_remote_node_context_is_ready(this_device->rnc))
   {
      // Since the RNC is ready, it's alright to finish completion
      // processing (e.g. signal the remote device is ready).
      scic_sds_stp_remote_device_ready_idle_substate_resume_complete_handler(
         this_device
      );
   }
   else
   {
      scic_sds_remote_node_context_resume(
         this_device->rnc,
         scic_sds_stp_remote_device_ready_idle_substate_resume_complete_handler,
         this_device
      );
   }
}

//*****************************************************************************
//*  STP REMOTE DEVICE READY CMD SUBSTATE
//*****************************************************************************

/**
 *
 *
 * @param[in] device This is the SCI base object which is cast into a
 *       SCIC_SDS_REMOTE_DEVICE object.
 *
 * @return none
 */
static
void scic_sds_stp_remote_device_ready_cmd_substate_enter(
   SCI_BASE_OBJECT_T * device
)
{
   SCIC_SDS_REMOTE_DEVICE_T * this_device;

   this_device = (SCIC_SDS_REMOTE_DEVICE_T *)device;

   ASSERT(this_device->working_request != NULL);

   SET_STATE_HANDLER(
      this_device,
      scic_sds_stp_remote_device_ready_substate_handler_table,
      SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_CMD
   );

   scic_cb_remote_device_not_ready(
      scic_sds_remote_device_get_controller(this_device),
      this_device,
      SCIC_REMOTE_DEVICE_NOT_READY_SATA_REQUEST_STARTED
   );
}

//*****************************************************************************
//*  STP REMOTE DEVICE READY NCQ SUBSTATE
//*****************************************************************************

/**
 *
 *
 * @param[in] device This is the SCI base object which is cast into a
 *       SCIC_SDS_REMOTE_DEVICE object.
 *
 * @return none
 */
static
void scic_sds_stp_remote_device_ready_ncq_substate_enter(
   SCI_BASE_OBJECT_T * device
)
{
   SCIC_SDS_REMOTE_DEVICE_T * this_device;

   this_device = (SCIC_SDS_REMOTE_DEVICE_T *)device;

   SET_STATE_HANDLER(
      this_device,
      scic_sds_stp_remote_device_ready_substate_handler_table,
      SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_NCQ
   );
}

//*****************************************************************************
//*  STP REMOTE DEVICE READY NCQ ERROR SUBSTATE
//*****************************************************************************

/**
 *
 *
 * @param[in] device This is the SCI base object which is cast into a
 *       SCIC_SDS_REMOTE_DEVICE object.
 *
 * @return none
 */
static
void scic_sds_stp_remote_device_ready_ncq_error_substate_enter(
   SCI_BASE_OBJECT_T * device
)
{
   SCIC_SDS_REMOTE_DEVICE_T * this_device;

   this_device = (SCIC_SDS_REMOTE_DEVICE_T *)device;

   SET_STATE_HANDLER(
      this_device,
      scic_sds_stp_remote_device_ready_substate_handler_table,
      SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_NCQ_ERROR
   );

   if(this_device->not_ready_reason ==
         SCIC_REMOTE_DEVICE_NOT_READY_SATA_SDB_ERROR_FIS_RECEIVED)
   {
      scic_cb_remote_device_not_ready(
         scic_sds_remote_device_get_controller(this_device),
         this_device,
         this_device->not_ready_reason
      );
   }
}

//*****************************************************************************
//*  STP REMOTE DEVICE READY AWAIT RESET SUBSTATE
//*****************************************************************************

/**
 * @brief The enter routine to READY AWAIT RESET substate.
 *
 * @param[in] device This is the SCI base object which is cast into a
 *       SCIC_SDS_REMOTE_DEVICE object.
 *
 * @return none
 */
static
void scic_sds_stp_remote_device_ready_await_reset_substate_enter(
   SCI_BASE_OBJECT_T * device
)
{
   SCIC_SDS_REMOTE_DEVICE_T * this_device;

   this_device = (SCIC_SDS_REMOTE_DEVICE_T *)device;

   SET_STATE_HANDLER(
      this_device,
      scic_sds_stp_remote_device_ready_substate_handler_table,
      SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_AWAIT_RESET
   );
}

#if !defined(DISABLE_ATAPI)
//*****************************************************************************
//*  STP REMOTE DEVICE READY ATAPI ERROR SUBSTATE
//*****************************************************************************

/**
 * @brief The enter routine to READY ATAPI ERROR substate.
 *
 * @param[in] device This is the SCI base object which is cast into a
 *       SCIC_SDS_REMOTE_DEVICE object.
 *
 * @return none
 */
static
void scic_sds_stp_remote_device_ready_atapi_error_substate_enter(
   SCI_BASE_OBJECT_T * device
)
{
   SCIC_SDS_REMOTE_DEVICE_T * this_device;

   this_device = (SCIC_SDS_REMOTE_DEVICE_T *)device;

   SET_STATE_HANDLER(
      this_device,
      scic_sds_stp_remote_device_ready_substate_handler_table,
      SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_ATAPI_ERROR
   );
}
#endif // !defined(DISABLE_ATAPI)

// ---------------------------------------------------------------------------

SCI_BASE_STATE_T
   scic_sds_stp_remote_device_ready_substate_table[
      SCIC_SDS_STP_REMOTE_DEVICE_READY_MAX_SUBSTATES] =
{
   {
      SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_IDLE,
      scic_sds_stp_remote_device_ready_idle_substate_enter,
      NULL
   },
   {
      SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_CMD,
      scic_sds_stp_remote_device_ready_cmd_substate_enter,
      NULL
   },
   {
      SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_NCQ,
      scic_sds_stp_remote_device_ready_ncq_substate_enter,
      NULL
   },
   {
      SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_NCQ_ERROR,
      scic_sds_stp_remote_device_ready_ncq_error_substate_enter,
      NULL
   },
#if !defined(DISABLE_ATAPI)
   {
      SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_ATAPI_ERROR,
      scic_sds_stp_remote_device_ready_atapi_error_substate_enter,
      NULL
   },
#endif
   {
      SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_AWAIT_RESET,
      scic_sds_stp_remote_device_ready_await_reset_substate_enter,
      NULL
   }
};

