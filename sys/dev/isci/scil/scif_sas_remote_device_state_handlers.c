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
 *        to the framework remote device state handler methods.
 */

#include <dev/isci/scil/scic_remote_device.h>

#include <dev/isci/scil/scif_sas_logger.h>
#include <dev/isci/scil/scif_sas_remote_device.h>
#include <dev/isci/scil/scif_sas_domain.h>
#include <dev/isci/scil/scif_sas_task_request.h>
#include <dev/isci/scil/scif_sas_internal_io_request.h>

//******************************************************************************
//* S T O P P E D   H A N D L E R S
//******************************************************************************

/**
 * @brief This method provides STOPPED state specific handling for
 *        when the framework attempts to start the remote device.  This
 *        method attempts to transition the state machine into the
 *        STARTING state.  If this is unsuccessful, then there is a direct
 *        transition into the FAILED state.
 *
 * @param[in]  remote_device This parameter specifies the remote device
 *             object for which the framework is attempting to start.
 *
 * @return This method returns an indication as to whether the start
 *         operating began successfully.
 */
static
SCI_STATUS scif_sas_remote_device_stopped_start_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T *)
                                          remote_device;

   sci_base_state_machine_change_state(
      &fw_device->parent.state_machine, SCI_BASE_REMOTE_DEVICE_STATE_STARTING
   );

   // Check to see if the state transition occurred without issue.
   if (sci_base_state_machine_get_state(&fw_device->parent.state_machine)
       == SCI_BASE_REMOTE_DEVICE_STATE_FAILED)
   {
      SCIF_LOG_WARNING((
         sci_base_object_get_logger(fw_device),
         SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
         "Domain:0x%x Device:0x%x Status:0x%x failed to start\n",
         fw_device->domain, fw_device, fw_device->operation_status
      ));
   }

   return fw_device->operation_status;
}

/**
 * @brief This method provides STOPPED state specific handling for
 *        when the user attempts to destruct the remote device.
 *
 * @param[in]  remote_device This parameter specifies the remote device
 *             object for which the framework is attempting to start.
 *
 * @return This method returns an indication as to whether the destruct
 *         operation completed successfully.
 */
static
SCI_STATUS scif_sas_remote_device_stopped_destruct_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device
)
{
   SCI_STATUS                 status;
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T *)
                                          remote_device;

   SMP_DISCOVER_RESPONSE_PROTOCOLS_T  dev_protocols;
   scic_remote_device_get_protocols(fw_device->core_object, &dev_protocols);

   //For smp device, need to clear its smp phy list first.
   if(dev_protocols.u.bits.attached_smp_target)
      scif_sas_smp_remote_device_removed(fw_device);

   status = scic_remote_device_destruct(fw_device->core_object);
   if (status == SCI_SUCCESS)
   {
      sci_base_state_machine_change_state(
         &fw_device->parent.state_machine, SCI_BASE_REMOTE_DEVICE_STATE_FINAL
      );

      scif_sas_remote_device_deinitialize_state_logging(fw_device);
   }
   else
   {
      SCIF_LOG_ERROR((
         sci_base_object_get_logger(fw_device),
         SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_REMOTE_DEVICE_CONFIG,
         "Device:0x%x Status:0x%x failed to destruct core device\n",
         fw_device
      ));
   }

   return status;
}

//******************************************************************************
//* S T O P P I N G   H A N D L E R S
//******************************************************************************

/**
 * @brief This method provides STOPPING state specific handling for
 *        when the core remote device object issues a stop completion
 *        notification.
 *
 * @note There is no need to ensure all IO/Task requests are complete
 *       before transitioning to the STOPPED state.  The SCI Core will
 *       ensure this is accomplished.
 *
 * @param[in]  remote_device This parameter specifies the remote device
 *             object for which the completion occurred.
 * @param[in]  completion_status This parameter specifies the status
 *             of the completion operation.
 *
 * @return none.
 */
static
void scif_sas_remote_device_stopping_stop_complete_handler(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   SCI_STATUS                 completion_status
)
{
   // Transition directly to the STOPPED state since the core ensures
   // all IO/Tasks are complete.
   sci_base_state_machine_change_state(
      &fw_device->parent.state_machine,
      SCI_BASE_REMOTE_DEVICE_STATE_STOPPED
   );

   if (completion_status != SCI_SUCCESS)
   {
      SCIF_LOG_ERROR((
         sci_base_object_get_logger(fw_device),
         SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_REMOTE_DEVICE_CONFIG,
         "Device:0x%x Status:0x%x failed to stop core device\n",
         fw_device, completion_status
      ));

      // Something is seriously wrong.  Stopping the core remote device
      // shouldn't fail in anyway.
      scif_cb_controller_error(fw_device->domain->controller,
              SCI_CONTROLLER_REMOTE_DEVICE_ERROR);
   }
}

/**
 * @brief This method provides STOPPING state handling for high priority
 *        IO requests, when the framework attempts to complete a high
 *        priority request.
 *
 * @param[in]  remote_device This parameter specifies the remote device
 *             object for which to complete the high priority IO.
 * @param[in]  io_request This parameter specifies the IO request to be
 *             completed.
 * @param[in]  response_data This parameter is ignored, since the device
 *             is in the stopping state.
 *
 * @return This method always returns success.
 */
static
SCI_STATUS scif_sas_remote_device_stopping_complete_high_priority_io_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request,
   void                     * response_data,
   SCI_IO_STATUS              completion_status
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T *)
                                          remote_device;
   SCIF_SAS_REQUEST_T       * fw_request = (SCIF_SAS_REQUEST_T *) io_request;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(remote_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_sas_remote_device_stopping_complete_high_priority_io_handler(0x%x,0x%x,0x%x) enter\n",
      remote_device, io_request, response_data
   ));

   fw_device->request_count--;

   if (fw_request->is_internal == TRUE)
   {
      scif_sas_internal_io_request_complete(
         fw_device->domain->controller,
         (SCIF_SAS_INTERNAL_IO_REQUEST_T *) io_request,
         SCI_SUCCESS
      );
   }

   return SCI_SUCCESS;
}

//******************************************************************************
//* F A I L E D   H A N D L E R S
//******************************************************************************

/**
 * @brief This method provides FAILED state specific handling for
 *        when the remote device is being stopped by the framework.
 *
 * @param[in]  remote_device This parameter specifies the remote device
 *             object for which the stop operation is being requested.
 *
 * @return This method returns an indication as to whether the failure
 *         operation completed successfully.
 */
static
SCI_STATUS scif_sas_remote_device_failed_stop_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T *)
                                          remote_device;

   SCIF_LOG_WARNING((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE,
      "RemoteDevice:0x%x stopping failed device\n",
      fw_device
   ));

   sci_base_state_machine_change_state(
      &fw_device->parent.state_machine, SCI_BASE_REMOTE_DEVICE_STATE_STOPPING
   );

   /// @todo Fix the return code handling.
   return SCI_FAILURE;
}

//******************************************************************************
//* D E F A U L T   H A N D L E R S
//******************************************************************************

/**
 * @brief This method provides default handling (i.e. returns an error);
 *        when a user attempts to start a remote device and a start operation
 *        is not allowed.
 *
 * @param[in]  remote_device This parameter specifies the remote device object
 *             on which the user is attempting to perform a start operation.
 *
 * @return This method returns an indication that start operations are not
 *         allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
SCI_STATUS scif_sas_remote_device_default_start_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger((SCIF_SAS_REMOTE_DEVICE_T *)remote_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_REMOTE_DEVICE_CONFIG,
      "RemoteDevice:0x%x State:0x%x invalid state to start\n",
      remote_device,
      sci_base_state_machine_get_state(
         &((SCIF_SAS_REMOTE_DEVICE_T *)remote_device)->parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * @brief This method provides default handling (i.e. returns an error);
 *        when a user attempts to stop a remote device and a stop operation
 *        is not allowed.
 *
 * @param[in]  remote_device This parameter specifies the remote device object
 *             on which the user is attempting to perform a stop operation.
 *
 * @return This method returns an indication that stop operations are not
 *         allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
SCI_STATUS scif_sas_remote_device_default_stop_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger((SCIF_SAS_REMOTE_DEVICE_T *)remote_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE,
      "RemoteDevice:0x%x State:0x%x invalid state to stop\n",
      remote_device,
      sci_base_state_machine_get_state(
         &((SCIF_SAS_REMOTE_DEVICE_T *)remote_device)->parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * @brief This method provides default handling (i.e. returns an error);
 *        when there is an attempt to fail a remote device from an invalid
 *        state.
 *
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which there is an attempt to fail the device.
 *
 * @return This method returns an indication that the fail transition is not
 *         allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
static
SCI_STATUS scif_sas_remote_device_default_fail_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger((SCIF_SAS_REMOTE_DEVICE_T *)remote_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE,
      "RemoteDevice:0x%x State:0x%x invalid state to fail device\n",
      remote_device,
      sci_base_state_machine_get_state(
         &((SCIF_SAS_REMOTE_DEVICE_T *)remote_device)->parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * @brief This method provides default handling (i.e. returns an error);
 *        when there is an attempt to destruct a remote device from an
 *        invalid state.
 *
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which there is an attempt to fail the device.
 *
 * @return This method returns an indication that the fail transition is not
 *         allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
SCI_STATUS scif_sas_remote_device_default_destruct_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger((SCIF_SAS_REMOTE_DEVICE_T *)remote_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE,
      "RemoteDevice:0x%x State:0x%x invalid state to destruct.\n",
      remote_device,
      sci_base_state_machine_get_state(
         &((SCIF_SAS_REMOTE_DEVICE_T *)remote_device)->parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * @brief This method provides default handling (i.e. returns an error);
 *        when there is an attempt to reset a remote device from an invalid
 *        state.
 *
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which there is an attempt to fail the device.
 *
 * @return This method returns an indication that the fail transition is not
 *         allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
SCI_STATUS scif_sas_remote_device_default_reset_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger((SCIF_SAS_REMOTE_DEVICE_T *)remote_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE,
      "RemoteDevice:0x%x State:0x%x invalid state to reset.\n",
      remote_device,
      sci_base_state_machine_get_state(
         &((SCIF_SAS_REMOTE_DEVICE_T *)remote_device)->parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * @brief This method provides default handling (i.e. returns an error);
 *        when there is an attempt to complete a reset to the remote device
 *        from an invalid state.
 *
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which there is an attempt to fail the device.
 *
 * @return This method returns an indication that the fail transition is not
 *         allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
SCI_STATUS scif_sas_remote_device_default_reset_complete_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger((SCIF_SAS_REMOTE_DEVICE_T *)remote_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE,
      "RemoteDevice:0x%x State:0x%x invalid state to complete reset.\n",
      remote_device,
      sci_base_state_machine_get_state(
         &((SCIF_SAS_REMOTE_DEVICE_T *)remote_device)->parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * @brief This method provides default handling (i.e. returns an error);
 *        when a user attempts to start an IO on a remote device and a start
 *        IO operation is not allowed.
 *
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which the user is attempting to perform a start IO
 *             operation.
 * @param[in]  io_request This parameter specifies the IO request to be
 *             started.
 *
 * @return This method returns an indication that start IO operations
 *         are not allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
SCI_STATUS scif_sas_remote_device_default_start_io_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger((SCIF_SAS_REMOTE_DEVICE_T *)remote_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE,
      "RemoteDevice:0x%x State:0x%x invalid state to start IO.\n",
      remote_device,
      sci_base_state_machine_get_state(
         &((SCIF_SAS_REMOTE_DEVICE_T *)remote_device)->parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * @brief This method provides default handling (i.e. returns an error);
 *        when a user attempts to complete an IO on a remote device and a
 *        complete IO operation is not allowed.
 *
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which the user is attempting to perform a complete
 *             IO operation.
 * @param[in]  io_request This parameter specifies the IO request to be
 *             completed.
 *
 * @return This method returns an indication that complete IO operations
 *         are not allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
SCI_STATUS scif_sas_remote_device_default_complete_io_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger((SCIF_SAS_REMOTE_DEVICE_T *)remote_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE,
      "RemoteDevice:0x%x State:0x%x invalid state to complete IO\n",
      remote_device,
      sci_base_state_machine_get_state(
         &((SCIF_SAS_REMOTE_DEVICE_T *)remote_device)->parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}


/**
 * @brief This method provides default handling (i.e. returns an error);
 *        when a user attempts to complete an IO on a remote device and a
 *        complete IO operation is not allowed.
 *
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which the user is attempting to perform a start IO
 *             operation.
 * @param[in]  io_request This parameter specifies the IO request to be
 *             started.
 *
 * @return This method returns an indication that complete IO operations
 *         are not allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
SCI_STATUS scif_sas_remote_device_default_complete_high_priority_io_handler(
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
 * @brief This method provides default handling (i.e. returns an error);
 *        when a user attempts to continue an IO on a remote device and a
 *        continue IO operation is not allowed.
 *
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which the user is attempting to perform a start IO
 *             operation.
 * @param[in]  io_request This parameter specifies the IO request to be
 *             started.
 *
 * @return This method returns an indication that continue IO operations
 *         are not allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
SCI_STATUS scif_sas_remote_device_default_continue_io_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger((SCIF_SAS_REMOTE_DEVICE_T *)remote_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE,
      "RemoteDevice:0x%x State:0x%x invalid state to continue IO\n",
      remote_device,
      sci_base_state_machine_get_state(
         &((SCIF_SAS_REMOTE_DEVICE_T *)remote_device)->parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * @brief This method provides default handling (i.e. returns an error);
 *        when a user attempts to start a task on a remote device and a
 *        start task operation is not allowed.
 *
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which the user is attempting to perform a start
 *             task operation.
 * @param[in]  task_request This parameter specifies the task management
 *             request to be started.
 *
 * @return This method returns an indication that start task operations
 *         are not allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
SCI_STATUS scif_sas_remote_device_default_start_task_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * task_request
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger((SCIF_SAS_REMOTE_DEVICE_T *)remote_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_TASK_MANAGEMENT,
      "RemoteDevice:0x%x State:0x%x invalid state to start task\n",
      remote_device,
      sci_base_state_machine_get_state(
         &((SCIF_SAS_REMOTE_DEVICE_T *)remote_device)->parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * @brief This method provides default handling (i.e. returns an error);
 *        when a user attempts to complete a task on a remote device and a
 *        complete task operation is not allowed.
 *
 * @param[in]  remote_device This parameter specifies the remote device object
 *             on which the user is attempting to perform a complete task
 *             operation.
 * @param[in]  task_request This parameter specifies the task management
 *             request to be completed.
 *
 * @return This method returns an indication that complete task operations
 *         are not allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
SCI_STATUS scif_sas_remote_device_default_complete_task_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * task_request
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger((SCIF_SAS_REMOTE_DEVICE_T *)remote_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_TASK_MANAGEMENT,
      "RemoteDevice:0x%x State:0x%x invalid state to complete task\n",
      remote_device,
      sci_base_state_machine_get_state(
         &((SCIF_SAS_REMOTE_DEVICE_T *)remote_device)->parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * @brief This method provides default handling (i.e. returns an error);
 *        for when the core issues a start completion notification and
 *        such a notification isn't supported.
 *
 * @param[in]  remote_device This parameter specifies the remote device object
 *             for which the completion notification has occurred.
 * @param[in]  completion_status This parameter specifies the status
 *             of the completion operation.
 *
 * @return none.
 */
void scif_sas_remote_device_default_start_complete_handler(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   SCI_STATUS                 completion_status
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE,
      "RemoteDevice:0x%x State:0x%x invalid state to start complete\n",
      fw_device,
      sci_base_state_machine_get_state(&fw_device->parent.state_machine)
   ));
}

/**
 * @brief This method provides default handling (i.e. returns an error);
 *        for when the core issues a stop completion notification and
 *        such a notification isn't supported.
 *
 * @param[in]  remote_device This parameter specifies the remote device object
 *             for which the completion notification has occurred.
 * @param[in]  completion_status This parameter specifies the status
 *             of the completion operation.
 *
 * @return none.
 */
void scif_sas_remote_device_default_stop_complete_handler(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   SCI_STATUS                 completion_status
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE,
      "RemoteDevice:0x%x State:0x%x invalid state to stop complete\n",
      fw_device,
      sci_base_state_machine_get_state(&fw_device->parent.state_machine)
   ));
}

/**
 * @brief This method provides default handling (i.e. returns an error);
 *        for when the core issues a ready notification and such a
 *        notification isn't supported.
 *
 * @param[in]  remote_device This parameter specifies the remote device object
 *             for which the notification has occurred.
 *
 * @return none.
 */
void scif_sas_remote_device_default_ready_handler(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE,
      "RemoteDevice:0x%x State:0x%x invalid state to handle ready\n",
      fw_device,
      sci_base_state_machine_get_state(&fw_device->parent.state_machine)
   ));
}

/**
 * @brief This method provides default handling (i.e. returns an error);
 *        for when the core issues a not ready notification and such a
 *        notification isn't supported.
 *
 * @param[in]  remote_device This parameter specifies the remote device object
 *             for which the notification has occurred.
 *
 * @return none.
 */
void scif_sas_remote_device_default_not_ready_handler(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   U32                        reason_code
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE,
      "RemoteDevice:0x%x State:0x%x invalid state to handle not ready\n",
      fw_device,
      sci_base_state_machine_get_state(&fw_device->parent.state_machine)
   ));
}

#if !defined(DISABLE_WIDE_PORTED_TARGETS)
/**
 * @brief This method provides handling of device start complete duing
 *        UPDATING_PORT_WIDTH state.
 *
 * @param[in]  remote_device This parameter specifies the remote device object
 *             which is start complete.
 *
 * @return none.
 */
static
SCI_STATUS scif_sas_remote_device_updating_port_width_state_complete_io_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T*)
                                          remote_device;
   fw_device->request_count--;

   //If the request count is zero, go ahead to update the RNC.
   if (fw_device->request_count == 0 )
   {
      if (fw_device->destination_state == SCIF_SAS_REMOTE_DEVICE_DESTINATION_STATE_STOPPING)
      {
         //if the destination state of this device change to STOPPING, no matter
         //whether we need to update the port width, just make the device
         //go to the STOPPING state, the device will be removed anyway.
         sci_base_state_machine_change_state(
            &fw_device->parent.state_machine,
            SCI_BASE_REMOTE_DEVICE_STATE_STOPPING
         );
      }
      else
      {
         //stop the device, upon the stop complete callback, start the device again
         //with the updated port width.
         scic_remote_device_stop(
            fw_device->core_object, SCIF_SAS_REMOTE_DEVICE_CORE_OP_TIMEOUT);
      }
   }

   return SCI_SUCCESS;
}


/**
 * @brief This method provides handling of device start complete duing
 *        UPDATING_PORT_WIDTH state.
 *
 * @param[in]  remote_device This parameter specifies the remote device object
 *             which is start complete.
 *
 * @return none.
 */
static
void scif_sas_remote_device_updating_port_width_state_start_complete_handler(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   SCI_STATUS                 completion_status
)
{
   SCIF_LOG_INFO((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE,
      "RemoteDevice:0x%x updating port width state start complete handler\n",
      fw_device,
      sci_base_state_machine_get_state(&fw_device->parent.state_machine)
   ));

   if ( fw_device->destination_state
           == SCIF_SAS_REMOTE_DEVICE_DESTINATION_STATE_STOPPING )
   {
      //if the destination state of this device change to STOPPING, no matter
      //whether we need to update the port width again, just make the device
      //go to the STOPPING state.
      sci_base_state_machine_change_state(
         &fw_device->parent.state_machine,
         SCI_BASE_REMOTE_DEVICE_STATE_STOPPING
      );
   }
   else if ( scic_remote_device_get_port_width(fw_device->core_object)
                != fw_device->device_port_width
            && fw_device->device_port_width != 0)
   {
      scic_remote_device_stop(
         fw_device->core_object,
         SCIF_SAS_REMOTE_DEVICE_CORE_OP_TIMEOUT
      );
   }
   else
   {
      //Port width updating succeeds. Transfer to destination state.
      sci_base_state_machine_change_state(
         &fw_device->parent.state_machine,
         SCI_BASE_REMOTE_DEVICE_STATE_READY
      );
   }
}

/**
 * @brief This method provides handling of device stop complete duing
 *        UPDATING_PORT_WIDTH state.
 *
 * @param[in]  remote_device This parameter specifies the remote device object
 *             which is stop complete.
 *
 * @return none.
 */
static
void scif_sas_remote_device_updating_port_width_state_stop_complete_handler(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   SCI_STATUS                 completion_status
)
{
   SCIF_LOG_INFO((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE,
      "RemoteDevice:0x%x updating port width state stop complete handler\n",
      fw_device,
      sci_base_state_machine_get_state(&fw_device->parent.state_machine)
   ));

   if ( fw_device->destination_state
           == SCIF_SAS_REMOTE_DEVICE_DESTINATION_STATE_STOPPING )
   {
      //Device directly transits to STOPPED STATE from UPDATING_PORT_WIDTH state,
      fw_device->domain->device_start_count--;

      //if the destination state of this device change to STOPPING, no matter
      //whether we need to update the port width again, just make the device
      //go to the STOPPED state.
      sci_base_state_machine_change_state(
         &fw_device->parent.state_machine,
         SCI_BASE_REMOTE_DEVICE_STATE_STOPPED
      );
   }
   else
   {
      scic_remote_device_set_port_width(
         fw_device->core_object,
         fw_device->device_port_width
      );

      //Device stop complete, means the RNC has been destructed. Now we need to
      //start core device so the RNC with updated port width will be posted.
      scic_remote_device_start(
         fw_device->core_object, SCIF_SAS_REMOTE_DEVICE_CORE_OP_TIMEOUT);
   }
}

/**
 * @brief This method provides handling (i.e. returns an error);
 *        when a user attempts to stop a remote device during the updating
 *        port width state, it will record the destination state for this
 *        device to be STOPPING, instead of usually READY state.
 *
 * @param[in]  remote_device This parameter specifies the remote device object
 *             on which the user is attempting to perform a stop operation.
 *
 * @return This method always return SCI_SUCCESS.
 */
static
SCI_STATUS scif_sas_remote_device_updating_port_width_state_stop_handler(
   SCI_BASE_REMOTE_DEVICE_T * remote_device
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device =
      (SCIF_SAS_REMOTE_DEVICE_T *)remote_device;

   SCIF_LOG_INFO((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE,
      "RemoteDevice:0x%x updating port width state stop handler\n",
      fw_device,
      sci_base_state_machine_get_state(&fw_device->parent.state_machine)
   ));

   //Can't stop the device right now. Remember the pending stopping request.
   //When exit the UPDATING_PORT_WIDTH state, we will check this variable
   //to decide which state to go.
   fw_device->destination_state =
      SCIF_SAS_REMOTE_DEVICE_DESTINATION_STATE_STOPPING;

   return SCI_SUCCESS;
}

#endif //#if !defined(DISABLE_WIDE_PORTED_TARGETS)

#define scif_sas_remote_device_stopping_complete_io_handler   \
        scif_sas_remote_device_ready_operational_complete_io_handler
#define scif_sas_remote_device_stopping_complete_task_handler \
        scif_sas_remote_device_ready_operational_complete_task_handler

SCIF_SAS_REMOTE_DEVICE_STATE_HANDLER_T
scif_sas_remote_device_state_handler_table[SCI_BASE_REMOTE_DEVICE_MAX_STATES] =
{
   // SCI_BASE_REMOTE_DEVICE_STATE_INITIAL
   {
      {
         scif_sas_remote_device_default_start_handler,
         scif_sas_remote_device_default_stop_handler,
         scif_sas_remote_device_default_fail_handler,
         scif_sas_remote_device_default_destruct_handler,
         scif_sas_remote_device_default_reset_handler,
         scif_sas_remote_device_default_reset_complete_handler,
         scif_sas_remote_device_default_start_io_handler,
         scif_sas_remote_device_default_complete_io_handler,
         scif_sas_remote_device_default_continue_io_handler,
         scif_sas_remote_device_default_start_task_handler,
         scif_sas_remote_device_default_complete_task_handler
      },
      scif_sas_remote_device_default_start_complete_handler,
      scif_sas_remote_device_default_stop_complete_handler,
      scif_sas_remote_device_default_ready_handler,
      scif_sas_remote_device_default_not_ready_handler,
      scif_sas_remote_device_default_start_io_handler,
      scif_sas_remote_device_default_complete_high_priority_io_handler
   },
   // SCI_BASE_REMOTE_DEVICE_STATE_STOPPED
   {
      {
         scif_sas_remote_device_stopped_start_handler,
         scif_sas_remote_device_default_stop_handler,
         scif_sas_remote_device_default_fail_handler,
         scif_sas_remote_device_stopped_destruct_handler,
         scif_sas_remote_device_default_reset_handler,
         scif_sas_remote_device_default_reset_complete_handler,
         scif_sas_remote_device_default_start_io_handler,
         scif_sas_remote_device_default_complete_io_handler,
         scif_sas_remote_device_default_continue_io_handler,
         scif_sas_remote_device_default_start_task_handler,
         scif_sas_remote_device_default_complete_task_handler
      },
      scif_sas_remote_device_default_start_complete_handler,
      scif_sas_remote_device_default_stop_complete_handler,
      scif_sas_remote_device_default_ready_handler,
      scif_sas_remote_device_default_not_ready_handler,
      scif_sas_remote_device_default_start_io_handler,
      scif_sas_remote_device_default_complete_high_priority_io_handler
   },
   // SCI_BASE_REMOTE_DEVICE_STATE_STARTING
   {
      {
         scif_sas_remote_device_default_start_handler,
         scif_sas_remote_device_default_stop_handler,
         scif_sas_remote_device_default_fail_handler,
         scif_sas_remote_device_default_destruct_handler,
         scif_sas_remote_device_default_reset_handler,
         scif_sas_remote_device_default_reset_complete_handler,
         scif_sas_remote_device_default_start_io_handler,
         scif_sas_remote_device_default_complete_io_handler,
         scif_sas_remote_device_default_continue_io_handler,
         scif_sas_remote_device_default_start_task_handler,
         scif_sas_remote_device_default_complete_task_handler
      },
      scif_sas_remote_device_default_start_complete_handler,
      scif_sas_remote_device_default_stop_complete_handler,
      scif_sas_remote_device_default_ready_handler,
      scif_sas_remote_device_default_not_ready_handler,
      scif_sas_remote_device_default_start_io_handler,
      scif_sas_remote_device_default_complete_high_priority_io_handler
   },
   // SCI_BASE_REMOTE_DEVICE_STATE_READY - see substate handlers
   {
      {
         scif_sas_remote_device_default_start_handler,
         scif_sas_remote_device_default_stop_handler,
         scif_sas_remote_device_default_fail_handler,
         scif_sas_remote_device_default_destruct_handler,
         scif_sas_remote_device_default_reset_handler,
         scif_sas_remote_device_default_reset_complete_handler,
         scif_sas_remote_device_default_start_io_handler,
         scif_sas_remote_device_default_complete_io_handler,
         scif_sas_remote_device_default_continue_io_handler,
         scif_sas_remote_device_default_start_task_handler,
         scif_sas_remote_device_default_complete_task_handler
      },
      scif_sas_remote_device_default_start_complete_handler,
      scif_sas_remote_device_default_stop_complete_handler,
      scif_sas_remote_device_default_ready_handler,
      scif_sas_remote_device_default_not_ready_handler,
      scif_sas_remote_device_default_start_io_handler,
      scif_sas_remote_device_default_complete_high_priority_io_handler
   },
   // SCI_BASE_REMOTE_DEVICE_STATE_STOPPING
   {
      {
         scif_sas_remote_device_default_start_handler,
         scif_sas_remote_device_default_stop_handler,
         scif_sas_remote_device_default_fail_handler,
         scif_sas_remote_device_default_destruct_handler,
         scif_sas_remote_device_default_reset_handler,
         scif_sas_remote_device_default_reset_complete_handler,
         scif_sas_remote_device_default_start_io_handler,
         scif_sas_remote_device_stopping_complete_io_handler,
         scif_sas_remote_device_default_continue_io_handler,
         scif_sas_remote_device_default_start_task_handler,
         scif_sas_remote_device_stopping_complete_task_handler
      },
      scif_sas_remote_device_default_start_complete_handler,
      scif_sas_remote_device_stopping_stop_complete_handler,
      scif_sas_remote_device_default_ready_handler,
      scif_sas_remote_device_default_not_ready_handler,
      scif_sas_remote_device_default_start_io_handler,
      scif_sas_remote_device_stopping_complete_high_priority_io_handler
   },
   // SCI_BASE_REMOTE_DEVICE_STATE_FAILED
   {
      {
         scif_sas_remote_device_default_start_handler,
         scif_sas_remote_device_failed_stop_handler,
         scif_sas_remote_device_default_fail_handler,
         scif_sas_remote_device_default_destruct_handler,
         scif_sas_remote_device_default_reset_handler,
         scif_sas_remote_device_default_reset_complete_handler,
         scif_sas_remote_device_default_start_io_handler,
         scif_sas_remote_device_default_complete_io_handler,
         scif_sas_remote_device_default_continue_io_handler,
         scif_sas_remote_device_default_start_task_handler,
         scif_sas_remote_device_default_complete_task_handler
      },
      scif_sas_remote_device_default_start_complete_handler,
      scif_sas_remote_device_default_stop_complete_handler,
      scif_sas_remote_device_default_ready_handler,
      scif_sas_remote_device_default_not_ready_handler,
      scif_sas_remote_device_default_start_io_handler,
      scif_sas_remote_device_default_complete_high_priority_io_handler
   },
   // SCI_BASE_REMOTE_DEVICE_STATE_RESETTING - is unused by framework
   {
      {
         scif_sas_remote_device_default_start_handler,
         scif_sas_remote_device_default_stop_handler,
         scif_sas_remote_device_default_fail_handler,
         scif_sas_remote_device_default_destruct_handler,
         scif_sas_remote_device_default_reset_handler,
         scif_sas_remote_device_default_reset_complete_handler,
         scif_sas_remote_device_default_start_io_handler,
         scif_sas_remote_device_default_complete_io_handler,
         scif_sas_remote_device_default_continue_io_handler,
         scif_sas_remote_device_default_start_task_handler,
         scif_sas_remote_device_default_complete_task_handler
      },
      scif_sas_remote_device_default_start_complete_handler,
      scif_sas_remote_device_default_stop_complete_handler,
      scif_sas_remote_device_default_ready_handler,
      scif_sas_remote_device_default_not_ready_handler,
      scif_sas_remote_device_default_start_io_handler,
      scif_sas_remote_device_default_complete_high_priority_io_handler
   },
#if !defined(DISABLE_WIDE_PORTED_TARGETS)
   // SCI_BASE_REMOTE_DEVICE_STATE_UPDATING_PORT_WIDTH
   {
      {
         scif_sas_remote_device_default_start_handler,
         scif_sas_remote_device_updating_port_width_state_stop_handler,
         scif_sas_remote_device_default_fail_handler,
         scif_sas_remote_device_default_destruct_handler,
         scif_sas_remote_device_default_reset_handler,
         scif_sas_remote_device_default_reset_complete_handler,
         scif_sas_remote_device_default_start_io_handler,
         scif_sas_remote_device_updating_port_width_state_complete_io_handler,
         scif_sas_remote_device_default_continue_io_handler,
         scif_sas_remote_device_default_start_task_handler,
         scif_sas_remote_device_default_complete_task_handler
      },
      scif_sas_remote_device_updating_port_width_state_start_complete_handler,
      scif_sas_remote_device_updating_port_width_state_stop_complete_handler,
      scif_sas_remote_device_default_ready_handler,
      scif_sas_remote_device_default_not_ready_handler,
      scif_sas_remote_device_default_start_io_handler,
      scif_sas_remote_device_default_complete_high_priority_io_handler
   },
#endif
   // SCI_BASE_REMOTE_DEVICE_STATE_FINAL
   {
      {
         scif_sas_remote_device_default_start_handler,
         scif_sas_remote_device_default_stop_handler,
         scif_sas_remote_device_default_fail_handler,
         scif_sas_remote_device_default_destruct_handler,
         scif_sas_remote_device_default_reset_handler,
         scif_sas_remote_device_default_reset_complete_handler,
         scif_sas_remote_device_default_start_io_handler,
         scif_sas_remote_device_default_complete_io_handler,
         scif_sas_remote_device_default_continue_io_handler,
         scif_sas_remote_device_default_start_task_handler,
         scif_sas_remote_device_default_complete_task_handler
      },
      scif_sas_remote_device_default_start_complete_handler,
      scif_sas_remote_device_default_stop_complete_handler,
      scif_sas_remote_device_default_ready_handler,
      scif_sas_remote_device_default_not_ready_handler,
      scif_sas_remote_device_default_start_io_handler,
      scif_sas_remote_device_default_complete_high_priority_io_handler
   }
};

