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
 * @brief This file contains all of the state handler routines for each
 *        of the domain states defined by the SCI_BASE_DOMAIN state
 *        machine.
 * @note
 *        - The discover method must be synchronized with the
 *          controller's completion handler.  The OS specific driver
 *          component is responsible for ensuring this occurs.  If the
 *          discovery method is called from within the call
 *          tree of the completion routine, then no action is necessary.
 */


#include <dev/isci/scil/scic_port.h>
#include <dev/isci/scil/scic_io_request.h>
#include <dev/isci/scil/scif_sas_logger.h>
#include <dev/isci/scil/scif_sas_domain.h>

//******************************************************************************
//* P R O T E C T E D   M E T H O D S
//******************************************************************************

//******************************************************************************
//* S T A R T I N G   H A N D L E R S
//******************************************************************************

static
SCI_STATUS scif_sas_domain_starting_port_ready_handler(
   SCI_BASE_DOMAIN_T * domain
)
{
   SCIF_LOG_TRACE((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_domain_starting_port_ready_handler(0x%x) enter\n",
      domain
   ));

   // The domain was previously completely stopped.  Now that the port is
   // ready we can transition the domain to the ready state.
   sci_base_state_machine_change_state(
      &domain->state_machine, SCI_BASE_DOMAIN_STATE_READY
   );

   return SCI_SUCCESS;
}

//******************************************************************************
//* R E A D Y   H A N D L E R S
//******************************************************************************

/**
 * @brief This method provides READY state specific handling for
 *        when a user attempts to discover a domain.
 *
 * @param[in]  domain This parameter specifies the domain object
 *             on which the user is attempting to perform a discover
 *             operation.
 *
 * @return This method returns an indication of whether the discover operation
 *         succeeded.
 * @retval SCI_SUCCESSS This value is returned when the discover operation
 *         begins successfully.
 */
static
SCI_STATUS scif_sas_domain_ready_discover_handler(
   SCI_BASE_DOMAIN_T * domain,
   U32                 op_timeout,
   U32                 device_timeout
)
{
   SCIF_SAS_DOMAIN_T * fw_domain = (SCIF_SAS_DOMAIN_T *)domain;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_domain_ready_discover_handler(0x%x, 0x%x, 0x%x) enter\n",
      domain, op_timeout, device_timeout
   ));

   fw_domain->operation.timeout        = op_timeout;
   fw_domain->operation.device_timeout = device_timeout;
   fw_domain->operation.status         = SCI_SUCCESS;

   scif_cb_timer_start(
      fw_domain->controller,
      fw_domain->operation.timer,
      fw_domain->operation.timeout
   );

   scif_sas_domain_transition_to_discovering_state(fw_domain);

   return fw_domain->operation.status;
}

/**
 * @brief This method provides READY state processing for reception of a
 *        port NOT ready notification from the core.
 *
 * @param[in]  domain This parameter specifies the domain object
 *             on which the core port has just come ready.
 *
 * @return
 */
static
SCI_STATUS scif_sas_domain_ready_port_not_ready_handler(
   SCI_BASE_DOMAIN_T * domain,
   U32                 reason_code
)
{
   SCIF_LOG_TRACE((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_domain_ready_port_not_ready_handler(0x%x, 0x%x) enter\n",
      domain,
      reason_code
   ));

   if (reason_code != SCIC_PORT_NOT_READY_HARD_RESET_REQUESTED)
   {
      // Change to the STOPPING state to cause existing request
      // completions to be terminated and devices removed.
      sci_base_state_machine_change_state(
         &domain->state_machine, SCI_BASE_DOMAIN_STATE_STOPPING
      );
   }

   return SCI_SUCCESS;
}

/**
 * @brief This method provides READY state specific handling for
 *        when a user attempts to start an IO request.
 *
 * @param[in]  domain This parameter specifies the domain object
 *             on which the user is attempting to perform a start IO
 *             operation.
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which the user is attempting to perform a start IO
 *             operation.
 * @param[in]  io_request This parameter specifies the io request that is
 *             being started.
 *
 * @return This method returns an indication of whether the start IO
 *         operation succeeded.
 * @retval SCI_SUCCESS This value is returned when the start IO operation
 *         begins successfully.
 */
static
SCI_STATUS scif_sas_domain_ready_start_io_handler(
   SCI_BASE_DOMAIN_T        * domain,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request
)
{
   SCIF_SAS_DOMAIN_T        * fw_domain  = (SCIF_SAS_DOMAIN_T*) domain;
   SCIF_SAS_REMOTE_DEVICE_T * fw_device  = (SCIF_SAS_REMOTE_DEVICE_T*)
                                           remote_device;
   SCIF_SAS_REQUEST_T       * fw_request = (SCIF_SAS_REQUEST_T*) io_request;
   SCI_STATUS                 status;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_sas_domain_ready_start_io_handler(0x%x, 0x%x, 0x%x) enter\n",
      domain, remote_device, io_request
   ));

   status = fw_device->state_handlers->parent.start_io_handler(
               &fw_device->parent, &fw_request->parent
            );

   if (status == SCI_SUCCESS)
   {
      // Add the IO to the list of outstanding requests on the domain.
      sci_fast_list_insert_tail(
         &fw_domain->request_list, &fw_request->list_element
      );
   }

   return status;
}

/**
 * @brief This method provides READY state specific handling for
 *        when a user attempts to complete an IO request.
 *
 * @param[in]  domain This parameter specifies the domain object
 *             on which the user is attempting to perform a complete IO
 *             operation.
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which the user is attempting to perform a complete
 *             IO operation.
 * @param[in]  io_request This parameter specifies the io request that is
 *             being completed.
 *
 * @return This method returns an indication of whether the complete IO
 *         operation succeeded.
 * @retval SCI_SUCCESS This value is returned when the complete IO operation
 *         is successful.
 */
static
SCI_STATUS scif_sas_domain_ready_complete_io_handler(
   SCI_BASE_DOMAIN_T        * domain,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T*)
                                          remote_device;
   SCIF_SAS_REQUEST_T       * fw_request= (SCIF_SAS_REQUEST_T*) io_request;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_sas_domain_ready_complete_io_handler(0x%x, 0x%x, 0x%x) enter\n",
      domain, remote_device, io_request
   ));

   // Remove the IO from the list of outstanding requests on the domain.
   sci_fast_list_remove_element(&fw_request->list_element);

   return fw_device->state_handlers->parent.complete_io_handler(
             &fw_device->parent, &fw_request->parent
          );
}

/**
 * @brief This method provides READY state specific handling for
 *        when a user attempts to continue an IO request.
 *
 * @param[in]  domain This parameter specifies the domain object
 *             on which the user is attempting to perform a continue IO
 *             operation.
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which the user is attempting to perform a start IO
 *             operation.
 * @param[in]  io_request This parameter specifies the io request that is
 *             being started.
 *
 * @return This method returns an indication of whether the continue IO
 *         operation succeeded.
 * @retval SCI_SUCCESS This value is returned when the continue IO operation
 *         begins successfully.
 */
static
SCI_STATUS scif_sas_domain_ready_continue_io_handler(
   SCI_BASE_DOMAIN_T        * domain,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request
)
{
   SCIF_LOG_TRACE((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_sas_domain_ready_continue_io_handler(0x%x, 0x%x, 0x%x) enter\n",
      domain, remote_device, io_request
   ));

   /// @todo fix return code handling.
   return SCI_FAILURE;
}

/**
 * @brief This method provides READY state specific handling for
 *        when a user attempts to start a task request.
 *
 * @param[in]  domain This parameter specifies the domain object
 *             on which the user is attempting to perform a start task
 *             operation.
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which the user is attempting to perform a start IO
 *             operation.
 * @param[in]  task_request This parameter specifies the task request that
 *             is being started.
 *
 * @return This method returns an indication of whether the start task
 *         operation succeeded.
 * @retval SCI_SUCCESS This value is returned when the start task operation
 *         begins successfully.
 */
static
SCI_STATUS scif_sas_domain_ready_start_task_handler(
   SCI_BASE_DOMAIN_T        * domain,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * task_request
)
{
   SCIF_SAS_DOMAIN_T        * fw_domain  = (SCIF_SAS_DOMAIN_T*) domain;
   SCIF_SAS_REMOTE_DEVICE_T * fw_device  = (SCIF_SAS_REMOTE_DEVICE_T*)
                                           remote_device;
   SCIF_SAS_REQUEST_T       * fw_request = (SCIF_SAS_REQUEST_T*) task_request;
   SCI_STATUS                 status;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_TASK_MANAGEMENT,
      "scif_sas_domain_ready_start_task_handler(0x%x, 0x%x, 0x%x) enter\n",
      domain, remote_device, task_request
   ));

   status = fw_device->state_handlers->parent.start_task_handler(
               &fw_device->parent, &fw_request->parent
            );

   if (status == SCI_SUCCESS)
   {
      // Add the task to the list of outstanding requests on the domain.
      sci_fast_list_insert_tail(
         &fw_domain->request_list, &fw_request->list_element
      );
   }

   return status;
}

/**
 * @brief This method provides READY state specific handling for
 *        when a user attempts to complete a task request.
 *
 * @param[in]  domain This parameter specifies the domain object
 *             on which the user is attempting to perform a complete task
 *             operation.
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which the user is attempting to perform a start IO
 *             operation.
 * @param[in]  task_request This parameter specifies the task request that
 *             is being started.
 *
 * @return This method returns an indication of whether the complete task
 *         operation succeeded.
 * @retval SCI_SUCCESS This value is returned when the complete task operation
 *         begins successfully.
 */
static
SCI_STATUS scif_sas_domain_ready_complete_task_handler(
   SCI_BASE_DOMAIN_T        * domain,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * task_request
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device  = (SCIF_SAS_REMOTE_DEVICE_T*)
                                           remote_device;
   SCIF_SAS_REQUEST_T       * fw_request = (SCIF_SAS_REQUEST_T*) task_request;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_TASK_MANAGEMENT,
      "scif_sas_domain_ready_complete_task_handler(0x%x, 0x%x, 0x%x) enter\n",
      domain, remote_device, task_request
   ));

   // Remove the IO from the list of outstanding requests on the domain.
   sci_fast_list_remove_element(&fw_request->list_element);

   return fw_device->state_handlers->parent.complete_task_handler(
             &fw_device->parent, &fw_request->parent
          );
}


/**
 * @brief This method provides READY state specific handling for when a user
 *        attempts to start a high priority IO request.
 *
 * @param[in]  domain This parameter specifies the domain object
 *             on which the user is attempting to perform a start high priority
 *             IO operation (which is exclusively for Phy Control hard reset).
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which the user is attempting to perform a start
 *             high priority IO operation.
 * @param[in]  io_request This parameter specifies the io request that is
 *             being started.
 *
 * @return This method returns an indication of whether the start IO
 *         operation succeeded.
 * @retval SCI_SUCCESS This value is returned when the start IO operation
 *         begins successfully.
 */
static
SCI_STATUS scif_sas_domain_ready_start_high_priority_io_handler(
   SCI_BASE_DOMAIN_T        * domain,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request
)
{
   SCIF_SAS_DOMAIN_T        * fw_domain  = (SCIF_SAS_DOMAIN_T*) domain;
   SCIF_SAS_REMOTE_DEVICE_T * fw_device  = (SCIF_SAS_REMOTE_DEVICE_T*)
                                           remote_device;
   SCIF_SAS_REQUEST_T       * fw_request = (SCIF_SAS_REQUEST_T*) io_request;
   SCI_STATUS                 status;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_sas_domain_ready_start_high_priority_request_handler(0x%x, 0x%x, 0x%x) enter\n",
      domain, remote_device, io_request
   ));

   status = fw_device->state_handlers->start_high_priority_io_handler(
               &fw_device->parent, &fw_request->parent
            );

   if (status == SCI_SUCCESS)
   {
      // Add the IO to the list of outstanding requests on the domain.

      // When domain is in READY state, this high priority io is likely
      // a smp Phy Control or Discover request sent to parent device of
      // a target device, which is to be Target Reset. This high priority
      // IO's probably has already been added to the domain's list as a
      // SCIF_SAS_TASK_REQUEST. We need to check if it is already on the
      // list.

      if ( ! sci_fast_list_is_on_this_list(
                &fw_domain->request_list, &fw_request->list_element))

         sci_fast_list_insert_tail(
            &fw_domain->request_list, &fw_request->list_element
         );
   }

   return status;
}


/**
 * @brief This method provides READY state specific handling for
 *        when a user attempts to complete an high priroity IO request.
 *
 * @param[in]  domain This parameter specifies the domain object
 *             on which the user is attempting to perform a complete high
 *             priority IO operation (which is exclusively for Phy Control
 *             hard reset).
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which the user is attempting to perform a complete
 *             IO operation.
 * @param[in]  io_request This parameter specifies the io request that is
 *             being completed.
 *
 * @return This method returns an indication of whether the complete IO
 *         operation succeeded.
 * @retval SCI_SUCCESS This value is returned when the complete IO operation
 *         is successful.
 */
static
SCI_STATUS scif_sas_domain_ready_complete_high_priority_io_handler(
   SCI_BASE_DOMAIN_T        * domain,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request,
   void                     * response_data,
   SCI_IO_STATUS              completion_status
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T*)
                                          remote_device;
   SCIF_SAS_REQUEST_T       * fw_request= (SCIF_SAS_REQUEST_T*) io_request;

   SCIC_TRANSPORT_PROTOCOL    protocol;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_sas_domain_ready_complete_high_priority_io_handler(0x%x, 0x%x, 0x%x, 0x%x) enter\n",
      domain, remote_device, io_request, response_data
   ));

   protocol = scic_io_request_get_protocol(fw_request->core_object);

   // If the request is an SMP HARD/LINK RESET request, then the request
   // came through the task management path (partially).  As a result,
   // the accounting for the request is managed in the task request
   // completion path.  Thus, only change the domain request counter if
   // the request is not an SMP target reset of some sort.
   if (
         (protocol != SCIC_SMP_PROTOCOL)
      || (fw_device->protocol_device.smp_device.current_activity !=
                SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_TARGET_RESET)
      )
   {
      sci_fast_list_remove_element(&fw_request->list_element);
   }

   return fw_device->state_handlers->complete_high_priority_io_handler(
             &fw_device->parent, &fw_request->parent, response_data, completion_status
          );
}

//******************************************************************************
//* S T O P P I N G   H A N D L E R S
//******************************************************************************

static
SCI_STATUS scif_sas_domain_stopping_device_stop_complete_handler(
   SCI_BASE_DOMAIN_T        * domain,
   SCI_BASE_REMOTE_DEVICE_T * remote_device
)
{
   SCIF_SAS_DOMAIN_T * fw_domain = (SCIF_SAS_DOMAIN_T *) domain;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN,
      "scif_sas_domain_stopping_device_stop_complete_handler(0x%x, 0x%x) enter\n",
      domain, remote_device
   ));

   // Attempt to transition to the stopped state.
   scif_sas_domain_transition_to_stopped_state(fw_domain);

   return SCI_SUCCESS;
}

/**
 * @brief This method provides STOPPING state specific handling for
 *        when a user attempts to complete an IO request.
 *
 * @param[in]  domain This parameter specifies the domain object
 *             on which the user is attempting to perform a complete IO
 *             operation.
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which the user is attempting to perform a complete
 *             IO operation.
 * @param[in]  io_request This parameter specifies the io request that is
 *             being completed.
 *
 * @return This method returns an indication of whether the complete IO
 *         operation succeeded.
 * @retval SCI_SUCCESS This value is returned when the complete IO operation
 *         is successful.
 */
static
SCI_STATUS scif_sas_domain_stopping_complete_io_handler(
   SCI_BASE_DOMAIN_T        * domain,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request
)
{
   SCIF_SAS_DOMAIN_T * fw_domain = (SCIF_SAS_DOMAIN_T *) domain;
   SCI_STATUS          status;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_sas_domain_stopping_complete_io_handler(0x%x, 0x%x, 0x%x) enter\n",
      domain, remote_device, io_request
   ));

   status = scif_sas_domain_ready_complete_io_handler(
               domain, remote_device, io_request
            );

   // Attempt to transition to the stopped state.
   scif_sas_domain_transition_to_stopped_state(fw_domain);

   return status;
}


/**
 * @brief This method provides STOPPING state specific handling for
 *        when a user attempts to complete an IO request.
 *
 * @param[in]  domain This parameter specifies the domain object
 *             on which the user is attempting to perform a complete IO
 *             operation.
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which the user is attempting to perform a complete
 *             IO operation.
 * @param[in]  io_request This parameter specifies the io request that is
 *             being completed.
 *
 * @return This method returns an indication of whether the complete IO
 *         operation succeeded.
 * @retval SCI_SUCCESS This value is returned when the complete IO operation
 *         is successful.
 */
static
SCI_STATUS scif_sas_domain_stopping_complete_high_priority_io_handler(
   SCI_BASE_DOMAIN_T        * domain,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request,
   void                     * response_data,
   SCI_IO_STATUS              completion_status
)
{
   SCIF_SAS_DOMAIN_T * fw_domain = (SCIF_SAS_DOMAIN_T *) domain;
   SCI_STATUS          status;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_sas_domain_stopping_complete_io_handler(0x%x, 0x%x, 0x%x) enter\n",
      domain, remote_device, io_request
   ));

   status = scif_sas_domain_ready_complete_high_priority_io_handler(
               domain, remote_device, io_request, response_data, completion_status
            );

   // Attempt to transition to the stopped state.
   scif_sas_domain_transition_to_stopped_state(fw_domain);

   return status;
}


/**
 * @brief This method provides STOPPING state specific handling for
 *        when a user attempts to complete a task request.
 *
 * @param[in]  domain This parameter specifies the domain object
 *             on which the user is attempting to perform a complete task
 *             operation.
 *
 * @return This method returns an indication of whether the complete task
 *         operation succeeded.
 * @retval SCI_SUCCESS This value is returned when the complete task operation
 *         begins successfully.
 */
static
SCI_STATUS scif_sas_domain_stopping_complete_task_handler(
   SCI_BASE_DOMAIN_T        * domain,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * task_request
)
{
   SCIF_SAS_DOMAIN_T * fw_domain = (SCIF_SAS_DOMAIN_T *) domain;
   SCI_STATUS          status;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_TASK_MANAGEMENT,
      "scif_sas_domain_stopping_complete_task_handler(0x%x, 0x%x, 0x%x) enter\n",
      domain, remote_device, task_request
   ));

   status = scif_sas_domain_ready_complete_task_handler(
               domain, remote_device, task_request
            );

   // Attempt to transition to the stopped state.
   scif_sas_domain_transition_to_stopped_state(fw_domain);

   return SCI_SUCCESS;
}

//******************************************************************************
//* D I S C O V E R I N G   H A N D L E R S
//******************************************************************************

/**
 * @brief This method provides DISCOVERING state specific processing for
 *        reception of a port NOT ready notification from the core.  A port
 *        NOT ready notification forces the discovery operation to complete
 *        in error.  Additionally, all IOs are aborted and devices removed.
 *
 * @param[in]  domain This parameter specifies the domain object
 *             for which the core port is no longer ready.
 *
 * @return
 */
static
SCI_STATUS scif_sas_domain_discovering_port_not_ready_handler(
   SCI_BASE_DOMAIN_T * domain,
   U32                 reason_code
)
{
   SCIF_LOG_TRACE((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_domain_discovering_port_not_ready_handler(0x%x, 0x%x) enter\n",
      domain,
      reason_code
   ));

   // Change to the STOPPING state to cause existing request
   // completions to be terminated and devices removed.
   sci_base_state_machine_change_state(
      &domain->state_machine, SCI_BASE_DOMAIN_STATE_STOPPING
   );

   return SCI_SUCCESS;
}

static
SCI_STATUS scif_sas_domain_discovering_device_start_complete_handler(
   SCI_BASE_DOMAIN_T        * domain,
   SCI_BASE_REMOTE_DEVICE_T * remote_device
)
{
   SCIF_SAS_DOMAIN_T * fw_domain = (SCIF_SAS_DOMAIN_T *)domain;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_domain_discovering_device_start_complete_handler(0x%x) enter\n",
      domain, remote_device
   ));

   //domain will decide what's next step.
   scif_sas_domain_continue_discover(fw_domain);

   return SCI_SUCCESS;
}

// ---------------------------------------------------------------------------

static
SCI_STATUS scif_sas_domain_discovering_device_stop_complete_handler(
   SCI_BASE_DOMAIN_T        * domain,
   SCI_BASE_REMOTE_DEVICE_T * remote_device
)
{
   SCIF_LOG_TRACE((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_domain_discovering_device_stop_complete_handler(0x%x) enter\n",
      domain, remote_device
   ));

   return SCI_FAILURE;
}


/**
 * @brief This method provides DISCOVERING state specific handling for when a user
 *        attempts to start a high priority IO request.
 *
 * @param[in]  domain This parameter specifies the domain object
 *             on which the user is attempting to perform a start IO
 *             operation.
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which the user is attempting to perform a start IO
 *             operation.
 * @param[in]  io_request This parameter specifies the io request that is
 *             being started.
 *
 * @return This method returns an indication of whether the start IO
 *         operation succeeded.
 * @retval SCI_SUCCESS This value is returned when the start IO operation
 *         begins successfully.
 */
static
SCI_STATUS scif_sas_domain_discovering_start_high_priority_io_handler(
   SCI_BASE_DOMAIN_T        * domain,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request
)
{
   SCIF_SAS_DOMAIN_T        * fw_domain  = (SCIF_SAS_DOMAIN_T*) domain;
   SCIF_SAS_REMOTE_DEVICE_T * fw_device  = (SCIF_SAS_REMOTE_DEVICE_T*)
                                           remote_device;
   SCIF_SAS_REQUEST_T       * fw_request = (SCIF_SAS_REQUEST_T*) io_request;
   SCI_STATUS                 status;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_sas_domain_discovery_start_high_priority_request_handler(0x%x, 0x%x, 0x%x) enter\n",
      domain, remote_device, io_request
   ));

   status = fw_device->state_handlers->start_high_priority_io_handler(
               &fw_device->parent, &fw_request->parent
            );

   if (status == SCI_SUCCESS)
   {
      // Add the IO to the list of outstanding requests on the domain.

      // It is possible this high priority IO's has already been added to
      // the domain's list as a SCIF_SAS_TASK_REQUEST. We need to check
      // if it is already on the list.
      if ( ! sci_fast_list_is_on_this_list(
               &fw_domain->request_list, &fw_request->list_element))

         sci_fast_list_insert_tail(
            &fw_domain->request_list, &fw_request->list_element
         );
   }

   return status;
}


/**
 * @brief This method provides DISCOVERING state specific handling for
 *        when a user attempts to complete an IO request.  User IOs are
 *        allowed to be completed during discovery.
 *
 * @param[in]  domain This parameter specifies the domain object
 *             on which the user is attempting to perform a complete IO
 *             operation.
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which the user is attempting to perform a complete
 *             IO operation.
 * @param[in]  io_request This parameter specifies the io request that is
 *             being completed.
 *
 * @return This method returns an indication of whether the complete IO
 *         operation succeeded.
 * @retval SCI_SUCCESS This value is returned when the complete IO operation
 *         is successful.
 */
static
SCI_STATUS scif_sas_domain_discovering_complete_io_handler(
   SCI_BASE_DOMAIN_T        * domain,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request
)
{
   SCIF_LOG_TRACE((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_sas_domain_discovering_complete_io_handler(0x%x, 0x%x, 0x%x) enter\n",
      domain, remote_device, io_request
   ));

   return scif_sas_domain_ready_complete_io_handler(
             domain, remote_device, io_request
          );
}

/**
 * @brief This method provides DISCOVERING state specific handling for
 *        when a user attempts to complete an high priroity IO request.  User
 *        IOs are allowed to be completed during discovery.
 *
 * @param[in]  domain This parameter specifies the domain object
 *             on which the user is attempting to perform a complete IO
 *             operation.
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which the user is attempting to perform a complete
 *             IO operation.
 * @param[in]  io_request This parameter specifies the io request that is
 *             being completed.
 *
 * @return This method returns an indication of whether the complete IO
 *         operation succeeded.
 * @retval SCI_SUCCESS This value is returned when the complete IO operation
 *         is successful.
 */
static
SCI_STATUS scif_sas_domain_discovering_complete_high_priority_io_handler(
   SCI_BASE_DOMAIN_T        * domain,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request,
   void                     * response_data,
   SCI_IO_STATUS              completion_status
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T*)
                                          remote_device;
   SCIF_SAS_REQUEST_T       * fw_request= (SCIF_SAS_REQUEST_T*) io_request;

   SCIC_TRANSPORT_PROTOCOL    protocol;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_sas_domain_discovering_complete_high_priority_io_handler(0x%x, 0x%x, 0x%x, 0x%x) enter\n",
      domain, remote_device, io_request, response_data
   ));

   protocol = scic_io_request_get_protocol(fw_request->core_object);

   // Remove the IO from the list of outstanding requests on the domain.

   // If the request is an SMP HARD/LINK RESET request, then the request
   // came through the task management path (partially).  As a result,
   // the accounting for the request is managed in the task request
   // completion path.  Thus, only change the domain request counter if
   // the request is not an SMP target reset of some sort.
   if (
         (protocol != SCIC_SMP_PROTOCOL)
      || (fw_device->protocol_device.smp_device.current_activity !=
                SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_TARGET_RESET)
   )
   {
      sci_fast_list_remove_element(&fw_request->list_element);
   }

   return fw_device->state_handlers->complete_high_priority_io_handler(
             &fw_device->parent, &fw_request->parent, response_data, completion_status
          );
}


/**
 * @brief This method provides DISCOVERING state specific handling for
 *        when the framework attempts to complete an IO request.  Internal
 *        Framework IOs allowed to be continued during discovery.
 *
 * @param[in]  domain This parameter specifies the domain object
 *             on which the user is attempting to perform a continue IO
 *             operation.
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which the user is attempting to perform a continue
 *             IO operation.
 * @param[in]  io_request This parameter specifies the io request that is
 *             being continued.
 *
 * @return This method returns an indication of whether the continue IO
 *         operation succeeded.
 * @retval SCI_SUCCESS This value is returned when the continue IO operation
 *         is successful.
 */
static
SCI_STATUS scif_sas_domain_discovering_continue_io_handler(
   SCI_BASE_DOMAIN_T        * domain,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request
)
{
   SCIF_LOG_TRACE((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_sas_domain_discovering_continue_io_handler(0x%x, 0x%x, 0x%x) enter\n",
      domain, remote_device, io_request
   ));

   /// @todo fix return code handling.
   return SCI_FAILURE;
}


/**
 * @brief This method provides handling when a user attempts to start
 *        a task on a domain in DISCOVER state, only hard reset is allowed.
 *
 * @param[in]  domain This parameter specifies the domain object
 *             on which the user is attempting to perform a start task
 *             operation.
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which the user is attempting to perform a start IO
 *             operation.
 * @param[in]  task_request This parameter specifies the task request that
 *             is being started.
 *
 * @return This method returns a status of start task operations
 * @retval SCI_FAILURE_INVALID_STATE This value is returned for any tasks,
 *         except for HARD RESET.
 */
static
SCI_STATUS scif_sas_domain_discovering_start_task_handler(
   SCI_BASE_DOMAIN_T        * domain,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * task_request
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T*)
                                          remote_device;
   SCIF_SAS_TASK_REQUEST_T  * fw_task = (SCIF_SAS_TASK_REQUEST_T*)task_request;

   //Only let target reset go through.
   if (scif_sas_task_request_get_function(fw_task)
             == SCI_SAS_HARD_RESET)
   {
      //If the domain is in the middle of smp DISCOVER process,
      //interrupt it. After target reset is done, resume the smp DISCOVERY.
      scif_sas_domain_cancel_smp_activities(fw_device->domain);

      return scif_sas_domain_ready_start_task_handler(domain, remote_device, task_request);
   }
   else{
      SCIF_LOG_WARNING((
         sci_base_object_get_logger(domain),
         SCIF_LOG_OBJECT_DOMAIN,
         "Domain:0x%x Device:0x%x State:0x%x start task message invalid\n",
         domain, remote_device,
         sci_base_state_machine_get_state(&domain->state_machine)
      ));

      return SCI_FAILURE_INVALID_STATE;
   }
}


/**
 * @brief This method provides DISCOVERING state specific handling for
 *        when a user attempts to complete a task request.  User task
 *        management requests are allowed to be completed during discovery.
 *
 * @param[in]  domain This parameter specifies the domain object
 *             on which the user is attempting to perform a complete IO
 *             operation.
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which the user is attempting to perform a complete
 *             IO operation.
 * @param[in]  task_request This parameter specifies the task request that
 *             is being completed.
 *
 * @return This method returns an indication of whether the complete task
 *         management operation succeeded.
 * @retval SCI_SUCCESS This value is returned when the complete task request
 *         is successful.
 */
static
SCI_STATUS scif_sas_domain_discovering_complete_task_handler(
   SCI_BASE_DOMAIN_T        * domain,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * task_request
)
{
   SCI_STATUS status;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_TASK_MANAGEMENT,
      "scif_sas_domain_discovering_complete_task_handler(0x%x, 0x%x, 0x%x) enter\n",
      domain, remote_device, task_request
   ));

   status = scif_sas_domain_ready_complete_task_handler(
               domain, remote_device, task_request
            );

   return status;
}

//******************************************************************************
//* D E F A U L T   H A N D L E R S
//******************************************************************************

/**
 * @brief This method provides default handling (i.e. returns an error)
 *        when a user attempts to discover a domain and a discovery
 *        operation is not allowed.
 *
 * @param[in]  domain This parameter specifies the domain object
 *             on which the user is attempting to perform an discover
 *             operation.
 * @param[in]  op_timeout This parameter specifies the timeout
 *             (in milliseconds) for the entire discovery operation.
 *             This timeout value should be some multiple of the
 *             individual device_timeout value.
 * @param[in]  device_timeout This parameter specifies the timeout
 *             (in milliseconds) for an individual device being discovered
 *             and configured during this operation.
 *
 * @return This method returns an indication that discovery operations
 *         are not allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
static
SCI_STATUS scif_sas_domain_default_discover_handler(
   SCI_BASE_DOMAIN_T * domain,
   U32                 op_timeout,
   U32                 device_timeout
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger((SCIF_SAS_DOMAIN_T *)domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "Domain:0x%x State:0x%x requested to discover in invalid state\n",
      domain,
      sci_base_state_machine_get_state(&domain->state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * @brief This method provides default processing for reception of a port
 *        ready notification from the core.
 *
 * @param[in]  domain This parameter specifies the domain object
 *             on which the core port has just come ready.
 *
 * @return
 */
static
SCI_STATUS scif_sas_domain_default_port_ready_handler(
   SCI_BASE_DOMAIN_T * domain
)
{
   SCIF_LOG_INFO((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN,
      "Domain:0x%x State:0x%x port now ready\n",
      domain,
      sci_base_state_machine_get_state(&domain->state_machine)
   ));

   return SCI_SUCCESS;
}

/**
 * @brief This method provides default processing for reception of a port
 *        NOT ready notification from the core.
 *
 * @param[in]  domain This parameter specifies the domain object
 *             on which the core port has just come ready.
 *
 * @return
 */
static
SCI_STATUS scif_sas_domain_default_port_not_ready_handler(
   SCI_BASE_DOMAIN_T * domain,
   U32                 reason_code
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN,
      "Domain:0x%x State:0x%x Port Not Ready 0x%x in invalid state\n",
      domain,
      sci_base_state_machine_get_state(&domain->state_machine),
      reason_code
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * @brief This method provides default handling (i.e. returns an error)
 *        when a user attempts to start an IO on a domain and a start
 *        IO operation is not allowed.
 *
 * @param[in]  domain This parameter specifies the domain object
 *             on which the user is attempting to perform a start IO
 *             operation.
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which the user is attempting to perform a start IO
 *             operation.
 * @param[in]  io_request This parameter specifies the io request that is
 *             being started.
 *
 * @return This method returns an indication that start IO operations
 *         are not allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
static
SCI_STATUS scif_sas_domain_default_start_io_handler(
   SCI_BASE_DOMAIN_T        * domain,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN,
      "Domain:0x%x Device:0x%x State:0x%x start IO message invalid\n",
      domain, remote_device,
      sci_base_state_machine_get_state(&domain->state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * @brief This method provides default handling (i.e. returns an error)
 *        when a user attempts to complete an IO on a domain and a
 *        complete IO operation is not allowed.
 *
 * @param[in]  domain This parameter specifies the domain object
 *             on which the user is attempting to perform a complete IO
 *             operation.
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which the user is attempting to perform a complete IO
 *             operation.
 * @param[in]  io_request This parameter specifies the io request that is
 *             being completed.
 *
 * @return This method returns an indication that complete IO operations
 *         are not allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
static
SCI_STATUS scif_sas_domain_default_complete_io_handler(
   SCI_BASE_DOMAIN_T        * domain,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN,
      "Domain:0x%x Device:0x%x State:0x%x complete IO message invalid\n",
      domain, remote_device,
      sci_base_state_machine_get_state(&domain->state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}


/**
 * @brief This method provides default handling (i.e. returns an error)
 *        when a user attempts to complete an IO on a domain and a
 *        complete IO operation is not allowed.
 *
 * @param[in]  domain This parameter specifies the domain object
 *             on which the user is attempting to perform a complete IO
 *             operation.
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which the user is attempting to perform a complete IO
 *             operation.
 * @param[in]  io_request This parameter specifies the io request that is
 *             being completed.
 *
 * @return This method returns an indication that complete IO operations
 *         are not allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
static
SCI_STATUS scif_sas_domain_default_complete_high_priority_io_handler(
   SCI_BASE_DOMAIN_T        * domain,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request,
   void                     * response_data,
   SCI_IO_STATUS              completion_status
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN,
      "Domain:0x%x Device:0x%x State:0x%x complete IO message invalid\n",
      domain, remote_device,
      sci_base_state_machine_get_state(&domain->state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * @brief This method provides default handling (i.e. returns an error)
 *        when a user attempts to continue an IO on a domain and a
 *        continue IO operation is not allowed.
 *
 * @param[in]  domain This parameter specifies the domain object
 *             on which the user is attempting to perform a continue IO
 *             operation.
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which the user is attempting to perform a start IO
 *             operation.
 * @param[in]  io_request This parameter specifies the io request that is
 *             being started.
 *
 * @return This method returns an indication that continue IO operations
 *         are not allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
static
SCI_STATUS scif_sas_domain_default_continue_io_handler(
   SCI_BASE_DOMAIN_T        * domain,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN,
      "Domain:0x%x Device:0x%x State:0x%x contineu IO message invalid\n",
      domain, remote_device,
      sci_base_state_machine_get_state(&domain->state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * @brief This method provides default handling (i.e. returns an error)
 *        when a user attempts to start a task on a domain and a start
 *        task operation is not allowed.
 *
 * @param[in]  domain This parameter specifies the domain object
 *             on which the user is attempting to perform a start task
 *             operation.
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which the user is attempting to perform a start IO
 *             operation.
 * @param[in]  task_request This parameter specifies the task request that
 *             is being started.
 *
 * @return This method returns an indication that start task operations
 *         are not allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
static
SCI_STATUS scif_sas_domain_default_start_task_handler(
   SCI_BASE_DOMAIN_T        * domain,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * task_request
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN,
      "Domain:0x%x Device:0x%x State:0x%x start task message invalid\n",
      domain, remote_device,
      sci_base_state_machine_get_state(&domain->state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * @brief This method provides default handling (i.e. returns an error)
 *        when a user attempts to complete a task on a domain and a
 *        complete task operation is not allowed.
 *
 * @param[in]  domain This parameter specifies the domain object
 *             on which the user is attempting to perform a complete task
 *             operation.
 * @param[in]  remote_device This parameter specifies the remote device
 *             object on which the user is attempting to perform a start IO
 *             operation.
 * @param[in]  task_request This parameter specifies the task request that
 *             is being started.
 *
 * @return This method returns an indication that complete task operations
 *         are not allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
static
SCI_STATUS scif_sas_domain_default_complete_task_handler(
   SCI_BASE_DOMAIN_T        * domain,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * task_request
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN,
      "Domain:0x%x Device:0x%x State:0x%x complete task message invalid\n",
      domain, remote_device,
      sci_base_state_machine_get_state(&domain->state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * @brief This method provides default handling (i.e. returns an error)
 *        when a remote device start operation completes in a state.
 *
 * @param[in]  domain This parameter specifies the domain object
 *             on which the remote device start operation is completing.
 * @param[in]  remote_device This parameter specifies the remote device
 *             for which the start operation is completing.
 *
 * @return This method returns an indication that start operation
 *         completion is not allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
static
SCI_STATUS scif_sas_domain_default_device_start_complete_handler(
   SCI_BASE_DOMAIN_T        * domain,
   SCI_BASE_REMOTE_DEVICE_T * remote_device
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN,
      "Domain:0x%x Device:0x%x State:0x%x device stop complete message invalid\n",
      domain, remote_device,
      sci_base_state_machine_get_state(&domain->state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * @brief This method provides default handling (i.e. returns an error)
 *        when a remote device stop operation completes in a state.
 *
 * @param[in]  domain This parameter specifies the domain object
 *             on which the remote device stop operation is completing.
 * @param[in]  remote_device This parameter specifies the remote device
 *             for which the stop operation is completing.
 *
 * @return This method returns an indication that stop operation
 *         completion is not allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
static
SCI_STATUS scif_sas_domain_default_device_stop_complete_handler(
   SCI_BASE_DOMAIN_T        * domain,
   SCI_BASE_REMOTE_DEVICE_T * remote_device
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN,
      "Domain:0x%x Device:0x%x State:0x%x device stop complete message invalid\n",
      domain, remote_device,
      sci_base_state_machine_get_state(&domain->state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * @brief This method provides default handling (i.e. returns an error)
 *        when sci user try to destruct a remote device of this domain.
 *
 * @param[in]  domain This parameter specifies the domain object
 *             on which the remote device is to be destructed.
 * @param[in]  remote_device This parameter specifies the remote device
 *             to be destructed.
 *
 * @return This method returns an indication that device destruction
 *         is not allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
static
SCI_STATUS scif_sas_domain_default_device_destruct_handler(
   SCI_BASE_DOMAIN_T        * domain,
   SCI_BASE_REMOTE_DEVICE_T * remote_device
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN,
      "Domain:0x%x Device:0x%x State:0x%x device destruct in invalid state\n",
      domain, remote_device,
      sci_base_state_machine_get_state(&domain->state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}


/**
 * @brief This method provides handling when sci user destruct a remote
 *        device of this domain in discovering state. Mainly the device
 *        is removed from domain's remote_device_list.
 *
 * @param[in]  domain This parameter specifies the domain object
 *             on which the remote device is to be destructed.
 * @param[in]  remote_device This parameter specifies the remote device
 *             to be destructed.
 *
 * @return This method returns a status of the device destruction.
 * @retval SCI_SUCCESS This value is returned when a remote device is
 *         successfully removed from domain.
 */
static
SCI_STATUS scif_sas_domain_discovering_device_destruct_handler(
   SCI_BASE_DOMAIN_T        * domain,
   SCI_BASE_REMOTE_DEVICE_T * remote_device
)
{
   SCIF_SAS_DOMAIN_T * fw_domain = (SCIF_SAS_DOMAIN_T *)domain;

   SCIF_LOG_WARNING((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN,
      "Domain:0x%x Device:0x%x State:0x%x device destruct in domain DISCOVERING state\n",
      domain, remote_device,
      sci_base_state_machine_get_state(&domain->state_machine)
   ));

   //remove the remote device from domain's remote_device_list
   sci_abstract_list_erase(
      &(fw_domain->remote_device_list),
      remote_device
   );

   return SCI_SUCCESS;
}


#define scif_sas_domain_stopped_discover_handler \
        scif_sas_domain_ready_discover_handler

#define scif_sas_domain_default_start_high_priority_io_handler \
        scif_sas_domain_default_start_io_handler


SCI_BASE_DOMAIN_STATE_HANDLER_T
   scif_sas_domain_state_handler_table[SCI_BASE_DOMAIN_MAX_STATES] =
{
   // SCI_BASE_DOMAIN_STATE_INITIAL
   {
      scif_sas_domain_default_discover_handler,
      scif_sas_domain_default_port_ready_handler,
      scif_sas_domain_default_port_not_ready_handler,
      scif_sas_domain_default_device_start_complete_handler,
      scif_sas_domain_default_device_stop_complete_handler,
      scif_sas_domain_default_device_destruct_handler,
      scif_sas_domain_default_start_io_handler,
      scif_sas_domain_default_start_high_priority_io_handler,
      scif_sas_domain_default_complete_io_handler,
      scif_sas_domain_default_complete_high_priority_io_handler,
      scif_sas_domain_default_continue_io_handler,
      scif_sas_domain_default_start_task_handler,
      scif_sas_domain_default_complete_task_handler
   },
   // SCI_BASE_DOMAIN_STATE_STARTING
   {
      scif_sas_domain_default_discover_handler,
      scif_sas_domain_starting_port_ready_handler,
      scif_sas_domain_default_port_not_ready_handler,
      scif_sas_domain_default_device_start_complete_handler,
      scif_sas_domain_default_device_stop_complete_handler,
      scif_sas_domain_default_device_destruct_handler,
      scif_sas_domain_default_start_io_handler,
      scif_sas_domain_default_start_high_priority_io_handler,
      scif_sas_domain_default_complete_io_handler,
      scif_sas_domain_default_complete_high_priority_io_handler,
      scif_sas_domain_default_continue_io_handler,
      scif_sas_domain_default_start_task_handler,
      scif_sas_domain_default_complete_task_handler
   },
   // SCI_BASE_DOMAIN_STATE_READY
   {
      scif_sas_domain_ready_discover_handler,
      scif_sas_domain_default_port_ready_handler,
      scif_sas_domain_ready_port_not_ready_handler,
      scif_sas_domain_default_device_start_complete_handler,
      scif_sas_domain_default_device_stop_complete_handler,
      scif_sas_domain_default_device_destruct_handler,
      scif_sas_domain_ready_start_io_handler,
      scif_sas_domain_ready_start_high_priority_io_handler,
      scif_sas_domain_ready_complete_io_handler,
      scif_sas_domain_ready_complete_high_priority_io_handler,
      scif_sas_domain_ready_continue_io_handler,
      scif_sas_domain_ready_start_task_handler,
      scif_sas_domain_ready_complete_task_handler
   },
   // SCI_BASE_DOMAIN_STATE_STOPPING
   {
      scif_sas_domain_default_discover_handler,
      scif_sas_domain_default_port_ready_handler,
      scif_sas_domain_default_port_not_ready_handler,
      scif_sas_domain_default_device_start_complete_handler,
      scif_sas_domain_stopping_device_stop_complete_handler,
      scif_sas_domain_default_device_destruct_handler,
      scif_sas_domain_default_start_io_handler,
      scif_sas_domain_default_start_high_priority_io_handler,
      scif_sas_domain_stopping_complete_io_handler,
      scif_sas_domain_stopping_complete_high_priority_io_handler,
      scif_sas_domain_default_continue_io_handler,
      scif_sas_domain_default_start_task_handler,
      scif_sas_domain_stopping_complete_task_handler
   },
   // SCI_BASE_DOMAIN_STATE_STOPPED
   {
      scif_sas_domain_stopped_discover_handler,
      scif_sas_domain_default_port_ready_handler,
      scif_sas_domain_default_port_not_ready_handler,
      scif_sas_domain_default_device_start_complete_handler,
      scif_sas_domain_default_device_stop_complete_handler,
      scif_sas_domain_default_device_destruct_handler,
      scif_sas_domain_default_start_io_handler,
      scif_sas_domain_default_start_high_priority_io_handler,
      scif_sas_domain_default_complete_io_handler,
      scif_sas_domain_default_complete_high_priority_io_handler,
      scif_sas_domain_default_continue_io_handler,
      scif_sas_domain_default_start_task_handler,
      scif_sas_domain_default_complete_task_handler
   },
   // SCI_BASE_DOMAIN_STATE_DISCOVERING
   {
      scif_sas_domain_default_discover_handler,
      scif_sas_domain_default_port_ready_handler,
      scif_sas_domain_discovering_port_not_ready_handler,
      scif_sas_domain_discovering_device_start_complete_handler,
      scif_sas_domain_discovering_device_stop_complete_handler,
      scif_sas_domain_discovering_device_destruct_handler,  //
      scif_sas_domain_default_start_io_handler,
      scif_sas_domain_discovering_start_high_priority_io_handler,
      scif_sas_domain_discovering_complete_io_handler,
      scif_sas_domain_discovering_complete_high_priority_io_handler, //
      scif_sas_domain_discovering_continue_io_handler,
      scif_sas_domain_discovering_start_task_handler,
      scif_sas_domain_discovering_complete_task_handler
   }
};

