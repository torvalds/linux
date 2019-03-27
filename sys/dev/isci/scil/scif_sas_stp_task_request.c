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

#include <dev/isci/scil/sati.h>
#include <dev/isci/scil/scif_sas_logger.h>
#include <dev/isci/scil/scif_sas_task_request.h>
#include <dev/isci/scil/scif_sas_controller.h>
#include <dev/isci/scil/scif_sas_remote_device.h>
#include <dev/isci/scil/scif_sas_stp_task_request.h>
#include <dev/isci/scil/scic_task_request.h>
#include <dev/isci/scil/scic_controller.h>

/**
 * @brief This method provides SATA/STP STARTED state specific handling for
 *        when the user attempts to complete the supplied IO request.
 *        It will perform data/response translation and free NCQ tags
 *        if necessary.
 *
 * @param[in] io_request This parameter specifies the IO request object
 *            to be started.
 *
 * @return This method returns a value indicating if the IO request was
 *         successfully completed or not.
 */
static
SCI_STATUS scif_sas_stp_core_cb_task_request_complete_handler(
   SCIF_SAS_CONTROLLER_T    * fw_controller,
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   SCIF_SAS_REQUEST_T       * fw_request,
   SCI_STATUS               * completion_status
)
{
#if !defined(DISABLE_SATI_TASK_MANAGEMENT)
   SCIF_SAS_TASK_REQUEST_T * fw_task = (SCIF_SAS_TASK_REQUEST_T *) fw_request;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_controller),
      SCIF_LOG_OBJECT_TASK_MANAGEMENT,
      "scif_sas_stp_core_cb_task_request_complete_handler(0x%x, 0x%x, 0x%x, 0x%x) enter\n",
      fw_controller, fw_device, fw_request, *completion_status
   ));

   // Translating the response is only necessary if some sort of error
   // occurred resulting in having the error bit set in the ATA status
   // register and values to decode in the ATA error register.
   if (  (*completion_status == SCI_SUCCESS)
      || (*completion_status == SCI_FAILURE_IO_RESPONSE_VALID) )
   {
      SATI_STATUS sati_status = sati_translate_task_response(
                                   &fw_task->parent.stp.sequence,
                                   fw_task,
                                   fw_task
                                );

      if (sati_status == SATI_COMPLETE)
         *completion_status = SCI_SUCCESS;
      else if (sati_status == SATI_FAILURE_CHECK_RESPONSE_DATA)
         *completion_status = SCI_FAILURE_IO_RESPONSE_VALID;
      else if (sati_status == SATI_SEQUENCE_INCOMPLETE)
      {
         // The translation indicates that additional SATA requests are
         // necessary to finish the original SCSI request.  As a result,
         // do not complete the IO and begin the next stage of the
         // translation.
         /// @todo multiple ATA commands are required, but not supported yet.
         return SCI_FAILURE;
      }
      else
      {
         // Something unexpected occurred during translation.  Fail the
         // IO request to the user.
         *completion_status = SCI_FAILURE;
      }
   }
   else  //A stp task request sometimes fails.
   {
      if (scif_sas_task_request_get_function(fw_task) == SCI_SAS_ABORT_TASK_SET)
      {
         scif_sas_stp_task_request_abort_task_set_failure_handler(
            fw_device, fw_task);
      }
   }

   return SCI_SUCCESS;
#else // !defined(DISABLE_SATI_TASK_MANAGEMENT)
   return SCI_FAILURE;
#endif // !defined(DISABLE_SATI_TASK_MANAGEMENT)
}

/**
 * @file
 *
 * @brief This file contains the method implementations for the
 *        SCIF_SAS_STP_TASK_REQUEST object.  The contents will implement
 *        SATA/STP specific functionality.
 */
SCI_STATUS scif_sas_stp_task_request_construct(
   SCIF_SAS_TASK_REQUEST_T * fw_task
)
{
   SCI_STATUS                 sci_status = SCI_FAILURE;

#if !defined(DISABLE_SATI_TASK_MANAGEMENT)
   SATI_STATUS                sati_status;
   SCIF_SAS_REMOTE_DEVICE_T * fw_device  = fw_task->parent.device;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_task),
      SCIF_LOG_OBJECT_TASK_MANAGEMENT,
      "scif_sas_stp_task_request_construct(0x%x) enter\n",
      fw_task
   ));

   // The translator will indirectly invoke core methods to set the fields
   // of the ATA register FIS inside of this method.
   sati_status = sati_translate_task_management(
                    &fw_task->parent.stp.sequence,
                    &fw_device->protocol_device.stp_device.sati_device,
                    fw_task,
                    fw_task
                 );

   if (sati_status == SATI_SUCCESS)
   {
      sci_status = scic_task_request_construct_sata(fw_task->parent.core_object);
      //fw_task->parent.state_handlers = &stp_io_request_constructed_handlers;
      fw_task->parent.protocol_complete_handler =
         scif_sas_stp_core_cb_task_request_complete_handler;
   }
   else
   {
      SCIF_LOG_ERROR((
         sci_base_object_get_logger(fw_task),
         SCIF_LOG_OBJECT_TASK_MANAGEMENT,
         "Task 0x%x received unexpected SAT translation failure 0x%x\n",
         fw_task, sati_status
      ));
   }
#endif // !defined(DISABLE_SATI_TASK_MANAGEMENT)

   return sci_status;
}


/**
 * @brief This method provides handling for failed stp TASK MANAGEMENT
 *           request.
 *
 * @param[in] fw_device This parameter specifies the target device the
 *            task management request towards to.
 * @param[in] fw_request This parameter specifies the failed task management
 *            request.
 * @param[in] completion_status This parameter sprecifies the completion
 *            status of the task management request's core status.
 *
 * @return None.
 */
void scif_sas_stp_task_request_abort_task_set_failure_handler(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   SCIF_SAS_TASK_REQUEST_T  * fw_task
)
{
#if !defined(DISABLE_SATI_TASK_MANAGEMENT)
   SCIF_SAS_DOMAIN_T         * fw_domain = fw_device->domain;
   SCI_FAST_LIST_ELEMENT_T   * pending_request_element;
   SCIF_SAS_REQUEST_T        * pending_request = NULL;

   pending_request_element = fw_domain->request_list.list_head;

   // Cycle through the list of IO requests. search all the
   // outstanding IOs with "waiting for abort task set" flag,
   // completes them now.
   while (pending_request_element != NULL)
   {
      pending_request =
         (SCIF_SAS_REQUEST_T*) sci_fast_list_get_object(pending_request_element);

      // The current element may be deleted from the list because of
      // IO completion so advance to the next element early
      pending_request_element = sci_fast_list_get_next(pending_request_element);

      if ( pending_request->device == fw_device
           && pending_request->is_waiting_for_abort_task_set == TRUE )
      {
         //In case the pending_request is still in the middle of aborting.
         //abort it again to the core.
         SCI_STATUS abort_status;

         //Reset the flag now since we are process the read log ext command now.
         pending_request->is_waiting_for_abort_task_set = FALSE;

         abort_status = scic_controller_terminate_request(
                           fw_domain->controller->core_object,
                           fw_device->core_object,
                           pending_request->core_object
                        );

         if (abort_status == SCI_FAILURE_INVALID_STATE)
         {
            //the request must have not be in aborting state anymore, complete it now.
            scif_cb_io_request_complete(
               fw_domain->controller,
               fw_device,
               pending_request,
               SCI_IO_FAILURE_TERMINATED
            );
         }
         //otherwise, the abort succeeded. Since the waiting flag is cleared,
         //the pending request will be completed later.
      }
   }
#endif //#if !defined(DISABLE_SATI_TASK_MANAGEMENT)
}
