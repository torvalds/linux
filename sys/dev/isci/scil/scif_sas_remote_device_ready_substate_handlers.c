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
 * @brief This file contains all of the method implementations pertaining
 *        to the framework remote device READY sub-state handler methods.
 */

#include <dev/isci/scil/scic_remote_device.h>
#include <dev/isci/scil/scic_io_request.h>

#include <dev/isci/scil/scif_sas_logger.h>
#include <dev/isci/scil/scif_sas_remote_device.h>
#include <dev/isci/scil/scif_sas_domain.h>
#include <dev/isci/scil/scif_sas_task_request.h>
#include <dev/isci/scil/scif_sas_io_request.h>
#include <dev/isci/scil/scif_sas_internal_io_request.h>
#include <dev/isci/scil/scif_sas_controller.h>
#include <dev/isci/scil/sci_abstract_list.h>
#include <dev/isci/scil/intel_sat.h>
#include <dev/isci/scil/sci_controller.h>

//******************************************************************************
//* P R I V A T E   M E T H O D S
//******************************************************************************

/**
 * @brief This method implements the behavior common to starting a task mgmt
 *        request.  It will change the ready substate to task management.
 *
 * @param[in]  fw_device This parameter specifies the remote device for
 *             which to complete a request.
 * @param[in]  fw_task This parameter specifies the task management
 *             request being started.
 *
 * @return This method returns a value indicating the status of the
 *         start operation.
 */
static
SCI_STATUS scif_sas_remote_device_start_task_request(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   SCIF_SAS_TASK_REQUEST_T  * fw_task
)
{
   // Transition into the TASK MGMT substate if not already in it.
   if (fw_device->ready_substate_machine.current_state_id
       != SCIF_SAS_REMOTE_DEVICE_READY_SUBSTATE_TASK_MGMT)
   {
      sci_base_state_machine_change_state(
         &fw_device->ready_substate_machine,
         SCIF_SAS_REMOTE_DEVICE_READY_SUBSTATE_TASK_MGMT
      );
   }

   fw_device->request_count++;
   fw_device->task_request_count++;

   return SCI_SUCCESS;
}

//******************************************************************************
//* R E A D Y   O P E R A T I O N A L   H A N D L E R S
//******************************************************************************

/**
 * @brief This method provides OPERATIONAL sub-state specific handling for
 *        when the core remote device object issues a device not ready
 *        notification.
 *
 * @param[in]  remote_device This parameter specifies the remote device
 *             object for which the notification occurred.
 *
 * @return none.
 */
static
void scif_sas_remote_device_ready_operational_not_ready_handler(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   U32                        reason_code
)
{
   if (reason_code == SCIC_REMOTE_DEVICE_NOT_READY_SATA_SDB_ERROR_FIS_RECEIVED)
   {
      sci_base_state_machine_change_state(
         &fw_device->ready_substate_machine,
         SCIF_SAS_REMOTE_DEVICE_READY_SUBSTATE_NCQ_ERROR
      );
   }
   else
   {
      // Even though we are in the OPERATIONAL state, the core remote device is not
      // ready.  As a result, we process user requests/events as if we were
      // stopping the framework remote device.
      sci_base_state_machine_change_state(
         &fw_device->ready_substate_machine,
         SCIF_SAS_REMOTE_DEVICE_READY_SUBSTATE_SUSPENDED
      );
   }
}

/**
 * @brief This method provides TASK MGMT sub-state specific handling for when
 *        the core remote device object issues a device not ready notification.
 *
 * @param[in]  remote_device This parameter specifies the remote device
 *             object for which the notification occurred.
 *
 * @return none.
 */
static
void scif_sas_remote_device_ready_task_management_not_ready_handler(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   U32                        reason_code
)
{
   //do nothing. Don't need to go to suspended substate.
}

/**
 * @brief This method provides OPERATIONAL sub-state specific handling for
 *        when the remote device is being stopped by the framework.
 *
 * @param[in]  remote_device This parameter specifies the remote device
 *             object for which the stop operation is being requested.
 *
 * @return This method returns an indication as to whether the failure
 *         operation completed successfully.
 */
static
SCI_STATUS scif_sas_remote_device_ready_operational_stop_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T *)
                                          remote_device;

   sci_base_state_machine_change_state(
      &fw_device->parent.state_machine, SCI_BASE_REMOTE_DEVICE_STATE_STOPPING
   );

   return fw_device->operation_status;
}

/**
 * @brief This method provides OPERATIONAL sub-state specific handling for
 *        when the user attempts to destruct the remote device.  In
 *        the READY state the framework must first stop the device
 *        before destructing it.
 *
 * @param[in]  remote_device This parameter specifies the remote device
 *             object for which the framework is attempting to start.
 *
 * @return This method returns an indication as to whether the destruct
 *         operation completed successfully.
 */
static
SCI_STATUS scif_sas_remote_device_ready_operational_destruct_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T *)
                                          remote_device;

   fw_device->destruct_when_stopped = TRUE;

   return (fw_device->state_handlers->parent.stop_handler(&fw_device->parent));
}

/**
 * @brief This method provides OPERATIONAL sub-state specific handling for
 *        when the remote device undergoes a failure condition.
 *
 * @param[in]  remote_device This parameter specifies the remote device
 *             object for which the failure condition occurred.
 *
 * @return This method returns an indication as to whether the failure
 *         operation completed successfully.
 */
static
SCI_STATUS scif_sas_remote_device_ready_operational_fail_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T *)
                                          remote_device;

   SCIF_LOG_WARNING((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE,
      "RemoteDevice:0x%x ready device failed\n",
      fw_device
   ));

   sci_base_state_machine_change_state(
      &fw_device->parent.state_machine, SCI_BASE_REMOTE_DEVICE_STATE_FAILED
   );

   /// @todo Fix the return code handling.
   return SCI_FAILURE;
}

/**
 * @brief This method provides OPERATIONAL sub-state specific handling for
 *        when a user attempts to start an IO request on a remote
 *        device.
 *
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which the user is attempting to perform a start
 *             IO operation.
 * @param[in]  io_request This parameter specifies the IO request to be
 *             started.
 *
 * @return This method returns an indication as to whether the IO request
 *         started successfully.
 */
static
SCI_STATUS scif_sas_remote_device_ready_operational_start_io_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T*)
                                          remote_device;
   SCIF_SAS_IO_REQUEST_T    * fw_io     = (SCIF_SAS_IO_REQUEST_T*) io_request;
   SCI_STATUS                 status;

   status = fw_io->parent.state_handlers->start_handler(&fw_io->parent.parent);

   if (status == SCI_SUCCESS)
   {
      fw_device->request_count++;
   }

   return status;
}

/**
 * @brief This method provides OPERATIONAL sub-state specific handling for
 *        when a user attempts to start an IO request on a remote
 *        device.
 *
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which the user is attempting to perform a complete
 *             IO operation.
 * @param[in]  io_request This parameter specifies the IO request to
 *             be completed.
 *
 * @return This method returns an indication as to whether the IO request
 *         completed successfully.
 */
SCI_STATUS scif_sas_remote_device_ready_operational_complete_io_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T*)
                                          remote_device;
   fw_device->request_count--;
   return SCI_SUCCESS;
}


/**
 * @brief This method provides OPERATIONAL sub-state specific handling for
 *        when a user attempts to start an IO request on a remote
 *        device.
 *
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which the user is attempting to perform a complete
 *             IO operation.
 * @param[in]  io_request This parameter specifies the IO request to
 *             be completed.
 *
 * @return This method returns an indication as to whether the IO request
 *         completed successfully.
 */
static
SCI_STATUS scif_sas_remote_device_ready_operational_complete_high_priority_io_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request,
   void                     * response_data,
   SCI_IO_STATUS              completion_status
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger((SCIF_SAS_REMOTE_DEVICE_T *)remote_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE,
      "RemoteDevice:0x%x State:0x%x invalid state to complete high priority IO\n",
      remote_device,
      sci_base_state_machine_get_state(
         &((SCIF_SAS_REMOTE_DEVICE_T *)remote_device)->parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}


/**
 * @brief This method provides OPERATIONAL sub-state specific handling for when
 *        the framework attempts to continue an IO request on a remote
 *        device.
 *
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which the user is attempting to perform a continue
 *             IO operation.
 * @param[in]  io_request This parameter specifies the IO request to
 *             be continued.
 *
 * @return This method returns an indication as to whether the IO request
 *         completed successfully.
 */
static
SCI_STATUS scif_sas_remote_device_ready_operational_continue_io_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request
)
{
   /// @todo Fix the return code handling.
   return SCI_FAILURE;
}

/**
 * @brief This method provides OPERATIONAL sub-state specific handling for
 *        when a user attempts to start a task management request on
 *        a remote device.  This includes terminating all of the affected
 *        ongoing IO requests (i.e. aborting them in the silicon) and then
 *        issuing the task management request to the silicon.
 *
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which the user is attempting to perform a start
 *             task operation.
 * @param[in]  task_request This parameter specifies the task management
 *             request to be started.
 *
 * @return This method returns an indication as to whether the task
 *         management request started successfully.
 */
static
SCI_STATUS scif_sas_remote_device_ready_operational_start_task_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * task_request
)
{
   SCI_STATUS                 status     = SCI_FAILURE;
   SCIF_SAS_REMOTE_DEVICE_T * fw_device  = (SCIF_SAS_REMOTE_DEVICE_T*)
                                           remote_device;
   SCIF_SAS_TASK_REQUEST_T  * fw_task    = (SCIF_SAS_TASK_REQUEST_T*)
                                           task_request;
   U8 task_function =
         scif_sas_task_request_get_function(fw_task);

   SMP_DISCOVER_RESPONSE_PROTOCOLS_T  dev_protocols;

   scic_remote_device_get_protocols(fw_device->core_object, &dev_protocols);
   if (   dev_protocols.u.bits.attached_ssp_target
       || dev_protocols.u.bits.attached_stp_target)
   {
      // //NOTE: For STP/SATA targets we currently terminate all requests for
      //       any type of task management.
      if (  (task_function == SCI_SAS_ABORT_TASK_SET)
         || (task_function == SCI_SAS_CLEAR_TASK_SET)
         || (task_function == SCI_SAS_LOGICAL_UNIT_RESET)
         || (task_function == SCI_SAS_I_T_NEXUS_RESET)
         || (task_function == SCI_SAS_HARD_RESET) )
      {
         // Terminate all of the requests in the silicon for this device.
         scif_sas_domain_terminate_requests(
            fw_device->domain, fw_device, NULL, fw_task
         );

         status = scif_sas_remote_device_start_task_request(fw_device, fw_task);
      }
      else if (  (task_function == SCI_SAS_CLEAR_ACA)
              || (task_function == SCI_SAS_QUERY_TASK)
              || (task_function == SCI_SAS_QUERY_TASK_SET)
              || (task_function == SCI_SAS_QUERY_ASYNCHRONOUS_EVENT) )
      {
       ASSERT(!dev_protocols.u.bits.attached_stp_target);
         status = scif_sas_remote_device_start_task_request(fw_device, fw_task);
      }
      else if (task_function == SCI_SAS_ABORT_TASK)
      {
         SCIF_SAS_REQUEST_T * fw_request
            = scif_sas_domain_get_request_by_io_tag(
                 fw_device->domain, fw_task->io_tag_to_manage
              );

         // Determine if the request being aborted was found.
         if (fw_request != NULL)
         {
            scif_sas_domain_terminate_requests(
               fw_device->domain, fw_device, fw_request, fw_task
            );

            status = scif_sas_remote_device_start_task_request(
                        fw_device, fw_task
                     );
         }
         else
            status = SCI_FAILURE_INVALID_IO_TAG;
      }
   }
   else
      status = SCI_FAILURE_UNSUPPORTED_PROTOCOL;

   if (status != SCI_SUCCESS)
   {
      SCIF_LOG_ERROR((
         sci_base_object_get_logger(fw_device),
         SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_TASK_MANAGEMENT,
         "Controller:0x%x TaskRequest:0x%x Status:0x%x start task failure\n",
         fw_device, fw_task, status
      ));
   }

   return status;
}

/**
 * @brief This method provides OPERATIONAL sub-state specific handling for
 *        when a user attempts to complete a task management request on
 *        a remote device.
 *
 * @param[in]  remote_device This parameter specifies the remote device object
 *             on which the user is attempting to perform a complete task
 *             operation.
 * @param[in]  task_request This parameter specifies the task management
 *             request to be completed.
 *
 * @return This method returns an indication as to whether the task
 *         management request succeeded.
 */
SCI_STATUS scif_sas_remote_device_ready_operational_complete_task_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * task_request
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T*)
                                          remote_device;
   fw_device->request_count--;
   fw_device->task_request_count--;

   return SCI_SUCCESS;
}

/**
 * @brief This method provides OPERATIONAL sub-state specific handling for
 *        when a user attempts to start a high priority IO request on a remote
 *        device.
 *
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which the user is attempting to perform a start
 *             IO operation.
 * @param[in]  io_request This parameter specifies the IO request to be
 *             started.
 *
 * @return This method returns an indication as to whether the IO request
 *         started successfully.
 */
static
SCI_STATUS scif_sas_remote_device_ready_operational_start_high_priority_io_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T*)
                                          remote_device;
   SCIF_SAS_IO_REQUEST_T    * fw_io     = (SCIF_SAS_IO_REQUEST_T*) io_request;

   SMP_DISCOVER_RESPONSE_PROTOCOLS_T  dev_protocols;

   scic_remote_device_get_protocols(fw_device->core_object, &dev_protocols);

   if (dev_protocols.u.bits.attached_smp_target)
   {
      //transit to task management state for smp request phase.
      if (fw_device->ready_substate_machine.current_state_id
       != SCIF_SAS_REMOTE_DEVICE_READY_SUBSTATE_TASK_MGMT)
      {
         sci_base_state_machine_change_state(
            &fw_device->ready_substate_machine,
            SCIF_SAS_REMOTE_DEVICE_READY_SUBSTATE_TASK_MGMT
         );
      }
   }

   fw_device->request_count++;

   return fw_io->parent.state_handlers->start_handler(&fw_io->parent.parent);
}


/**
 * @brief This method provides TASK MANAGEMENT sub-state specific handling for
 *        when a user attempts to complete a task management request on
 *        a remote device.
 *
 * @param[in]  remote_device This parameter specifies the remote device object
 *             on which the user is attempting to perform a complete task
 *             operation.
 * @param[in]  task_request This parameter specifies the task management
 *             request to be completed.
 *
 * @return This method returns an indication as to whether the task
 *         management request succeeded.
 */
SCI_STATUS scif_sas_remote_device_ready_task_management_complete_task_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * task_request
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T*)
                                          remote_device;

   SCIF_SAS_TASK_REQUEST_T * fw_task = (SCIF_SAS_TASK_REQUEST_T *)
                                       task_request;

   fw_device->request_count--;
   fw_device->task_request_count--;

   // All existing task management requests and all of the IO requests
   // affectected by the task management request must complete before
   // the remote device can transition back into the READY / OPERATIONAL
   // state.
   if (  (fw_device->task_request_count == 0)
      && (fw_task->affected_request_count == 0) )
   {
      sci_base_state_machine_change_state(
         &fw_device->ready_substate_machine,
         SCIF_SAS_REMOTE_DEVICE_READY_SUBSTATE_OPERATIONAL
      );
   }

   return SCI_SUCCESS;
}

/**
 * @brief This method provides SUSPENDED sub-state specific handling for
 *        when the core remote device object issues a device ready
 *        notification.  This effectively causes the framework remote
 *        device to transition back into the OPERATIONAL state.
 *
 * @param[in]  remote_device This parameter specifies the remote device
 *             object for which the notification occurred.
 *
 * @return none.
 */
static
void scif_sas_remote_device_ready_suspended_ready_handler(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device
)
{
   sci_base_state_machine_change_state(
      &fw_device->ready_substate_machine,
      SCIF_SAS_REMOTE_DEVICE_READY_SUBSTATE_OPERATIONAL
   );
}


/**
 * @brief This handler is currently solely used by smp remote device for
 *        discovering.
 *
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which the user is attempting to perform a complete high
 *             priority IO operation.
 * @param[in]  io_request This parameter specifies the high priority IO request
 *             to be completed.
 *
 * @return SCI_STATUS indicate whether the io complete successfully.
 */
SCI_STATUS
scif_sas_remote_device_ready_task_management_complete_high_priority_io_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request,
   void                     * response_data,
   SCI_IO_STATUS              completion_status
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device  = (SCIF_SAS_REMOTE_DEVICE_T*)
                                           remote_device;
   SCIF_SAS_REQUEST_T       * fw_request = (SCIF_SAS_REQUEST_T*) io_request;
   SCI_STATUS                 status     = SCI_SUCCESS;
   SCIC_TRANSPORT_PROTOCOL    protocol;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(remote_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_sas_remote_device_ready_task_management_complete_high_priority_io_handler(0x%x, 0x%x, 0x%x, 0x%x) enter\n",
      remote_device, io_request, response_data, completion_status
   ));

   fw_device->request_count--;

   // we are back to ready operational sub state here.
   sci_base_state_machine_change_state(
      &fw_device->ready_substate_machine,
      SCIF_SAS_REMOTE_DEVICE_READY_SUBSTATE_OPERATIONAL
   );

   protocol = scic_io_request_get_protocol(fw_request->core_object);

   // If this request was an SMP initiator request we created, then
   // decode the response.
   if (protocol == SCIC_SMP_PROTOCOL)
   {
      if (completion_status != SCI_IO_FAILURE_TERMINATED)
      {
         status = scif_sas_smp_remote_device_decode_smp_response(
                     fw_device, fw_request, response_data, completion_status
                  );
      }
      else
         scif_sas_smp_remote_device_terminated_request_handler(fw_device, fw_request);
   }
   else
   {
      // Currently, there are only internal SMP requests.  So, default work
      // is simply to clean up the internal request.
      if (fw_request->is_internal == TRUE)
      {
         scif_sas_internal_io_request_complete(
            fw_device->domain->controller,
            (SCIF_SAS_INTERNAL_IO_REQUEST_T *)fw_request,
            SCI_SUCCESS
         );
      }
   }

   return status;
}


SCIF_SAS_REMOTE_DEVICE_STATE_HANDLER_T
scif_sas_remote_device_ready_substate_handler_table[] =
{
   // SCIF_SAS_REMOTE_DEVICE_READY_SUBSTATE_OPERATIONAL
   {
      {
         scif_sas_remote_device_default_start_handler,
         scif_sas_remote_device_ready_operational_stop_handler,
         scif_sas_remote_device_ready_operational_fail_handler,
         scif_sas_remote_device_ready_operational_destruct_handler,
         scif_sas_remote_device_default_reset_handler,
         scif_sas_remote_device_default_reset_complete_handler,
         scif_sas_remote_device_ready_operational_start_io_handler,
         scif_sas_remote_device_ready_operational_complete_io_handler,
         scif_sas_remote_device_ready_operational_continue_io_handler,
         scif_sas_remote_device_ready_operational_start_task_handler,
         scif_sas_remote_device_ready_operational_complete_task_handler
      },
      scif_sas_remote_device_default_start_complete_handler,
      scif_sas_remote_device_default_stop_complete_handler,
      scif_sas_remote_device_default_ready_handler,
      scif_sas_remote_device_ready_operational_not_ready_handler,
      scif_sas_remote_device_ready_operational_start_high_priority_io_handler,  //
      scif_sas_remote_device_ready_operational_complete_high_priority_io_handler
   },
   // SCIF_SAS_REMOTE_DEVICE_READY_SUBSTATE_SUSPENDED
   {
      {
         scif_sas_remote_device_default_start_handler,
         scif_sas_remote_device_ready_operational_stop_handler,
         scif_sas_remote_device_ready_operational_fail_handler,
         scif_sas_remote_device_ready_operational_destruct_handler,
         scif_sas_remote_device_default_reset_handler,
         scif_sas_remote_device_default_reset_complete_handler,
         scif_sas_remote_device_default_start_io_handler,
         scif_sas_remote_device_ready_operational_complete_io_handler,
         scif_sas_remote_device_default_continue_io_handler,
         scif_sas_remote_device_ready_operational_start_task_handler,
         scif_sas_remote_device_ready_operational_complete_task_handler
      },
      scif_sas_remote_device_default_start_complete_handler,
      scif_sas_remote_device_default_stop_complete_handler,
      scif_sas_remote_device_ready_suspended_ready_handler,
      scif_sas_remote_device_default_not_ready_handler,
      scif_sas_remote_device_default_start_io_handler,
      scif_sas_remote_device_ready_operational_complete_high_priority_io_handler
   },
   // SCIF_SAS_REMOTE_DEVICE_READY_SUBSTATE_TASK_MGMT
   {
      {
         scif_sas_remote_device_default_start_handler,
         scif_sas_remote_device_ready_operational_stop_handler,
         scif_sas_remote_device_ready_operational_fail_handler,
         scif_sas_remote_device_ready_operational_destruct_handler,
         scif_sas_remote_device_default_reset_handler,
         scif_sas_remote_device_default_reset_complete_handler,
         scif_sas_remote_device_default_start_io_handler,
         scif_sas_remote_device_ready_operational_complete_io_handler,
         scif_sas_remote_device_ready_operational_continue_io_handler,
         scif_sas_remote_device_ready_operational_start_task_handler,
         scif_sas_remote_device_ready_task_management_complete_task_handler
      },
      scif_sas_remote_device_default_start_complete_handler,
      scif_sas_remote_device_default_stop_complete_handler,
      scif_sas_remote_device_default_ready_handler,
      scif_sas_remote_device_ready_task_management_not_ready_handler,
      scif_sas_remote_device_ready_operational_start_high_priority_io_handler,
      scif_sas_remote_device_ready_task_management_complete_high_priority_io_handler
   },
   // SCIF_SAS_REMOTE_DEVICE_READY_SUBSTATE_NCQ_ERROR
   {
      {
         scif_sas_remote_device_default_start_handler,
         scif_sas_remote_device_ready_operational_stop_handler,
         scif_sas_remote_device_ready_operational_fail_handler,
         scif_sas_remote_device_ready_operational_destruct_handler,
         scif_sas_remote_device_default_reset_handler,
         scif_sas_remote_device_default_reset_complete_handler,
         scif_sas_remote_device_default_start_io_handler,
         scif_sas_remote_device_ready_operational_complete_io_handler,
         scif_sas_remote_device_default_continue_io_handler,
         scif_sas_remote_device_ready_operational_start_task_handler,
         scif_sas_remote_device_ready_operational_complete_task_handler
      },
      scif_sas_remote_device_default_start_complete_handler,
      scif_sas_remote_device_default_stop_complete_handler,
      scif_sas_remote_device_ready_suspended_ready_handler,
      scif_sas_remote_device_default_not_ready_handler,
      scif_sas_remote_device_default_start_io_handler,
      scif_sas_remote_device_ready_operational_complete_high_priority_io_handler
   },
};

