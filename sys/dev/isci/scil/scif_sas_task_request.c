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
 * @brief This file contains the task management request object
 *        (SCIF_SAS_TASK_REQUEST) method implementations.
 */


#include <dev/isci/scil/intel_sas.h>

#include <dev/isci/scil/scic_task_request.h>
#include <dev/isci/scil/scic_remote_device.h>
#include <dev/isci/scil/scic_user_callback.h>
#include <dev/isci/scil/scic_controller.h>
#include <dev/isci/scil/scif_user_callback.h>

#include <dev/isci/scil/scif_sas_request.h>
#include <dev/isci/scil/scif_sas_task_request.h>
#include <dev/isci/scil/scif_sas_stp_task_request.h>
#include <dev/isci/scil/scif_sas_logger.h>
#include <dev/isci/scil/scif_sas_controller.h>
#include <dev/isci/scil/scif_sas_domain.h>
#include <dev/isci/scil/scif_sas_remote_device.h>
#include <dev/isci/scil/scif_sas_smp_io_request.h>

//******************************************************************************
//* P U B L I C   M E T H O D S
//******************************************************************************

U32 scif_task_request_get_object_size(
   void
)
{
   return (sizeof(SCIF_SAS_TASK_REQUEST_T) + scic_task_request_get_object_size());
}

// ---------------------------------------------------------------------------

U8 scif_sas_task_request_get_function(
   SCIF_SAS_TASK_REQUEST_T *fw_task
)
{
   return fw_task->function;
}

// ---------------------------------------------------------------------------

static
SCI_STATUS scif_sas_task_request_generic_construct(
   SCI_CONTROLLER_HANDLE_T      scif_controller,
   SCI_REMOTE_DEVICE_HANDLE_T   scif_remote_device,
   U16                          io_tag,
   void                       * user_task_request_object,
   void                       * task_request_memory,
   SCI_TASK_REQUEST_HANDLE_T  * scif_task_request,
   U8                           task_function
)
{
   SCI_STATUS                 status;
   SCIF_SAS_CONTROLLER_T    * fw_controller   = (SCIF_SAS_CONTROLLER_T*)
                                                scif_controller;
   SCIF_SAS_TASK_REQUEST_T  * fw_task         = (SCIF_SAS_TASK_REQUEST_T*)
                                                task_request_memory;
   SCIF_SAS_REMOTE_DEVICE_T * fw_device       = (SCIF_SAS_REMOTE_DEVICE_T*)
                                                scif_remote_device;
   U8                       * core_request_memory;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_controller),
      SCIF_LOG_OBJECT_TASK_MANAGEMENT,
      "scif_task_request_construct(0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x) enter\n",
      scif_controller, scif_remote_device, io_tag, user_task_request_object,
      task_request_memory, scif_task_request
   ));

   // Initialize the user's handle to the framework task request.
   *scif_task_request = fw_task;

   // initialize affected request count
   fw_task->affected_request_count = 0;
   fw_task->io_tag_to_manage = SCI_CONTROLLER_INVALID_IO_TAG;
   fw_task->function = task_function;

   if (task_function == SCI_SAS_HARD_RESET )
   {
      if (fw_device->containing_device != NULL )
      {// Target Reset is for an expander attached device,
       // go down to construct smp Phy Control request.
         scif_sas_smp_request_construct_phy_control(
            fw_controller,
            fw_device->containing_device,
            PHY_OPERATION_HARD_RESET,
            fw_device->expander_phy_identifier,
            user_task_request_object,
            task_request_memory
         );
      }
      else
      {
         scif_sas_request_construct(
            &fw_task->parent,
            fw_device,
            sci_base_object_get_logger(fw_controller),
            scif_sas_task_request_state_table
         );

         // If target reset is for a DA device, don't build task at all.
         // Just set object association.
         sci_object_set_association(fw_task, user_task_request_object);
      }

      return SCI_SUCCESS;
   }

   // Construct the parent object first in order to ensure logging can
   // function.
   scif_sas_request_construct(
      &fw_task->parent,
      fw_device,
      sci_base_object_get_logger(fw_controller),
      scif_sas_task_request_state_table
   );

   core_request_memory = (U8 *)task_request_memory + sizeof(SCIF_SAS_TASK_REQUEST_T);

   status = scic_task_request_construct(
               fw_controller->core_object,
               fw_device->core_object,
               io_tag,
               fw_task,
               core_request_memory,
               &fw_task->parent.core_object
            );

   if (status == SCI_SUCCESS)
   {
      SMP_DISCOVER_RESPONSE_PROTOCOLS_T  dev_protocols;

      // These associations must be set early for the core io request
      // object construction to complete correctly as there will be
      // callbacks into the user driver framework during core construction
      sci_object_set_association(fw_task, user_task_request_object);
      sci_object_set_association(fw_task->parent.core_object, fw_task);

      // Perform protocol specific core IO request construction.
      scic_remote_device_get_protocols(fw_device->core_object, &dev_protocols);
      if (dev_protocols.u.bits.attached_ssp_target)
         status = scic_task_request_construct_ssp(fw_task->parent.core_object);
      else if (dev_protocols.u.bits.attached_stp_target)
         status = scif_sas_stp_task_request_construct(fw_task);
      else
         status = SCI_FAILURE_UNSUPPORTED_PROTOCOL;

      if (status == SCI_SUCCESS)
      {
         sci_base_state_machine_logger_initialize(
            &fw_task->parent.parent.state_machine_logger,
            &fw_task->parent.parent.state_machine,
            &fw_task->parent.parent.parent,
            scif_cb_logger_log_states,
            "SCIF_SAS_TASK_REQUEST_T", "base_state_machine",
            SCIF_LOG_OBJECT_TASK_MANAGEMENT
         );
      }
      else
      {
         SCIF_LOG_WARNING((
            sci_base_object_get_logger(fw_task),
            SCIF_LOG_OBJECT_TASK_MANAGEMENT,
            "Device:0x%x TaskRequest:0x%x Function:0x%x construct failed\n",
            fw_device, fw_task, scif_sas_task_request_get_function(fw_task)
         ));
      }
   }

   return status;
}

// ---------------------------------------------------------------------------

SCI_STATUS scif_sas_internal_task_request_construct(
   SCI_CONTROLLER_HANDLE_T      scif_controller,
   SCI_REMOTE_DEVICE_HANDLE_T   scif_remote_device,
   U16                          io_tag,
   void                       * task_request_memory,
   SCI_TASK_REQUEST_HANDLE_T  * scif_task_request,
   U8                           task_function
)
{
   SCI_STATUS                 status;
   SCIF_SAS_TASK_REQUEST_T *  fw_task;

   status = scif_sas_task_request_generic_construct(
               scif_controller,
               scif_remote_device,
               io_tag,
               NULL,
               task_request_memory,
               scif_task_request,
               task_function
            );

   fw_task = (SCIF_SAS_TASK_REQUEST_T *)task_request_memory;

   fw_task->parent.is_internal = TRUE;

   return status;
}

// ---------------------------------------------------------------------------

SCI_STATUS scif_task_request_construct(
   SCI_CONTROLLER_HANDLE_T      scif_controller,
   SCI_REMOTE_DEVICE_HANDLE_T   scif_remote_device,
   U16                          io_tag,
   void                       * user_task_request_object,
   void                       * task_request_memory,
   SCI_TASK_REQUEST_HANDLE_T  * scif_task_request
)
{
   SCI_STATUS  status;
   U8          task_function =
                scif_cb_task_request_get_function(user_task_request_object);

   status = scif_sas_task_request_generic_construct(
               scif_controller,
               scif_remote_device,
               io_tag,
               user_task_request_object,
               task_request_memory,
               scif_task_request,
               task_function
            );

   return status;
}

// ---------------------------------------------------------------------------

void scif_sas_internal_task_request_destruct(
   SCIF_SAS_TASK_REQUEST_T * fw_internal_task
)
{
   SCIF_SAS_CONTROLLER_T * fw_controller =
      fw_internal_task->parent.device->domain->controller;
   scif_sas_controller_free_internal_request(fw_controller, fw_internal_task);
}

// ---------------------------------------------------------------------------

void scic_cb_task_request_complete(
   SCI_CONTROLLER_HANDLE_T     controller,
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device,
   SCI_TASK_REQUEST_HANDLE_T   task_request,
   SCI_TASK_STATUS             completion_status
)
{
   SCIF_SAS_CONTROLLER_T    * fw_controller = (SCIF_SAS_CONTROLLER_T*)
                                         sci_object_get_association(controller);
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T*)
                                      sci_object_get_association(remote_device);
   SCIF_SAS_TASK_REQUEST_T  * fw_task = (SCIF_SAS_TASK_REQUEST_T*)
                                       sci_object_get_association(task_request);
   SCI_STATUS                 status;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_controller),
      SCIF_LOG_OBJECT_TASK_MANAGEMENT,
      "scic_cb_task_request_complete(0x%x, 0x%x, 0x%x, 0x%x) enter\n",
      controller, remote_device, task_request, completion_status
   ));

   status = fw_task->parent.state_handlers->complete_handler(
               &fw_task->parent.parent
            );

   if (status == SCI_SUCCESS)
   {
      if (fw_task->parent.protocol_complete_handler != NULL)
      {
         status = fw_task->parent.protocol_complete_handler(
            fw_controller, fw_device, &fw_task->parent, (SCI_STATUS *)&completion_status
         );
      }

      if (status == SCI_SUCCESS)
      {
         SCIF_LOG_WARNING((
            sci_base_object_get_logger(fw_task),
            SCIF_LOG_OBJECT_TASK_MANAGEMENT,
            "RemoteDevice:0x%x TaskRequest:0x%x Function:0x%x CompletionStatus:0x%x "
            "completed\n",
            fw_device, fw_task,
            scif_sas_task_request_get_function(fw_task),
            completion_status
         ));

         // If this isn't an internal framework IO request, then simply pass the
         // notification up to the SCIF user.  Otherwise, immediately complete the
         // task since there is no SCIF user to notify.
         if (fw_task->parent.is_internal == FALSE)
         {
            scif_cb_task_request_complete(
               fw_controller, fw_device, fw_task, completion_status
            );
         }
         else
         {
            scif_controller_complete_task(
               fw_controller,
               fw_device,
               fw_task
            );
         }
      }
   }
}

// ---------------------------------------------------------------------------

U32 scic_cb_ssp_task_request_get_lun(
   void * scic_user_task_request
)
{
   SCIF_SAS_TASK_REQUEST_T * fw_task = (SCIF_SAS_TASK_REQUEST_T*)
                                       scic_user_task_request;

   fw_task->parent.lun = scif_cb_task_request_get_lun(
                            fw_task->parent.parent.parent.associated_object
                         );

   return fw_task->parent.lun;
}

// ---------------------------------------------------------------------------

U8 scic_cb_ssp_task_request_get_function(
   void * scic_user_task_request
)
{
   SCIF_SAS_TASK_REQUEST_T * fw_task = (SCIF_SAS_TASK_REQUEST_T*)
                                       scic_user_task_request;

   return scif_sas_task_request_get_function(fw_task);
}

// ---------------------------------------------------------------------------

U16 scic_cb_ssp_task_request_get_io_tag_to_manage(
   void * scic_user_task_request
)
{
   SCIF_SAS_TASK_REQUEST_T * fw_task = (SCIF_SAS_TASK_REQUEST_T*)
                                       scic_user_task_request;

   fw_task->io_tag_to_manage
      = scif_cb_task_request_get_io_tag_to_manage(
           fw_task->parent.parent.parent.associated_object
        );

   return fw_task->io_tag_to_manage;
}

// ---------------------------------------------------------------------------

void * scic_cb_ssp_task_request_get_response_data_address(
   void * scic_user_task_request
)
{
   SCIF_SAS_TASK_REQUEST_T * fw_task = (SCIF_SAS_TASK_REQUEST_T*)
                                       scic_user_task_request;

   return scif_cb_task_request_get_response_data_address(
                fw_task->parent.parent.parent.associated_object
          );
}

// ---------------------------------------------------------------------------

U32 scic_cb_ssp_task_request_get_response_data_length(
   void * scic_user_task_request
)
{
   SCIF_SAS_TASK_REQUEST_T * fw_task = (SCIF_SAS_TASK_REQUEST_T*)
                                       scic_user_task_request;

   return scif_cb_task_request_get_response_data_length(
             fw_task->parent.parent.parent.associated_object
          );
}

//******************************************************************************
//* P R O T E C T E D   M E T H O D S
//******************************************************************************

/**
 * @brief This method performs functionality required after a task management
 *        operation (either a task management request or a silicon task
 *        termination) has finished.
 *
 * @param[in]  fw_task This parameter specifies the request that has
 *             the operation completing.
 *
 * @return none
 */
void scif_sas_task_request_operation_complete(
   SCIF_SAS_TASK_REQUEST_T * fw_task
)
{
   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_task),
      SCIF_LOG_OBJECT_TASK_MANAGEMENT,
      "scif_sas_task_request_operation_complete(0x%x) enter\n",
      fw_task
   ));

   fw_task->affected_request_count--;

   SCIF_LOG_INFO((
      sci_base_object_get_logger(fw_task),
      SCIF_LOG_OBJECT_TASK_MANAGEMENT,
      "TaskRequest:0x%x current affected request count:0x%x\n",
      fw_task, fw_task->affected_request_count
   ));
}

