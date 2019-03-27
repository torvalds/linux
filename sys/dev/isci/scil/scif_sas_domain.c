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
 * @brief This file contains the implementation of the SCIF_SAS_DOMAIN
 *        object.
 */

#include <dev/isci/scil/intel_sas.h>
#include <dev/isci/scil/sci_fast_list.h>
#include <dev/isci/scil/scic_controller.h>
#include <dev/isci/scil/scic_port.h>
#include <dev/isci/scil/scic_remote_device.h>
#include <dev/isci/scil/scic_io_request.h>
#include <dev/isci/scil/scic_user_callback.h>
#include <dev/isci/scil/scif_user_callback.h>
#include <dev/isci/scil/sci_abstract_list.h>
#include <dev/isci/scil/sci_base_iterator.h>

#include <dev/isci/scil/scif_sas_logger.h>
#include <dev/isci/scil/scif_sas_domain.h>
#include <dev/isci/scil/scif_sas_controller.h>
#include <dev/isci/scil/scif_sas_remote_device.h>
#include <dev/isci/scil/scif_sas_smp_remote_device.h>
#include <dev/isci/scil/sci_util.h>

//******************************************************************************
//* P R I V A T E   M E T H O D S
//******************************************************************************

/**
 * @brief This method will attempt to handle an operation timeout (i.e.
 *        discovery or reset).
 *
 * @param[in]  cookie This parameter specifies the domain in which the
 *             timeout occurred.
 *
 * @return none
 */
static
void scif_sas_domain_operation_timeout_handler(
   void * cookie
)
{
   SCIF_SAS_DOMAIN_T * fw_domain = (SCIF_SAS_DOMAIN_T*) cookie;
   U32                 state;

   state = sci_base_state_machine_get_state(&fw_domain->parent.state_machine);

   // Based upon the state of the domain, we know whether we were in the
   // process of performing discovery or a reset.
   if (state == SCI_BASE_DOMAIN_STATE_DISCOVERING)
   {
      SCIF_LOG_WARNING((
         sci_base_object_get_logger(fw_domain),
         SCIF_LOG_OBJECT_DOMAIN,
         "Domain:0x%x State:0x%x DISCOVER timeout!\n",
         fw_domain, state
      ));

      fw_domain->operation.status = SCI_FAILURE_TIMEOUT;

      //search all the smp devices in the domain and cancel their activities
      //if there is any outstanding activity remained. The smp devices will terminate
      //all the started internal IOs.
      scif_sas_domain_cancel_smp_activities(fw_domain);

      scif_sas_domain_continue_discover(fw_domain);
   }
   else
   {
      SCIF_LOG_ERROR((
         sci_base_object_get_logger(fw_domain),
         SCIF_LOG_OBJECT_DOMAIN,
         "Domain:0x%x State:0x%x operation timeout in invalid state\n",
         fw_domain, state
      ));
   }
}

//******************************************************************************
//* P U B L I C   M E T H O D S
//******************************************************************************

SCI_PORT_HANDLE_T scif_domain_get_scic_port_handle(
   SCI_DOMAIN_HANDLE_T  domain
)
{
   SCIF_SAS_DOMAIN_T * fw_domain = (SCIF_SAS_DOMAIN_T*) domain;

   if ( (fw_domain == NULL) || (fw_domain->core_object == SCI_INVALID_HANDLE) )
      return SCI_INVALID_HANDLE;

   SCIF_LOG_WARNING((
      sci_base_object_get_logger(fw_domain),
      SCIF_LOG_OBJECT_DOMAIN,
      "Domain:0x%x no associated core port found\n",
      fw_domain
   ));

   return fw_domain->core_object;
}

// ---------------------------------------------------------------------------

SCI_REMOTE_DEVICE_HANDLE_T scif_domain_get_device_by_sas_address(
   SCI_DOMAIN_HANDLE_T   domain,
   SCI_SAS_ADDRESS_T   * sas_address
)
{
   SCIF_SAS_DOMAIN_T        * fw_domain = (SCIF_SAS_DOMAIN_T*) domain;
   SCI_ABSTRACT_ELEMENT_T   * element   = sci_abstract_list_get_front(
                                             &fw_domain->remote_device_list
                                          );
   SCIF_SAS_REMOTE_DEVICE_T * fw_device;
   SCI_SAS_ADDRESS_T          fw_device_address;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN,
      "scif_domain_get_device_by_sas_address(0x%x, 0x%x) enter\n",
      domain, sas_address
   ));

   // Search the abstract list to see if there is a remote device with the
   // same SAS address.
   while (element != NULL)
   {
      fw_device = (SCIF_SAS_REMOTE_DEVICE_T*)
                  sci_abstract_list_get_object(element);

      scic_remote_device_get_sas_address(
         fw_device->core_object, &fw_device_address
      );

      // Check to see if this is the device for which we are searching.
      if (  (fw_device_address.low == sas_address->low)
         && (fw_device_address.high == sas_address->high) )
      {
         return fw_device;
      }

      element = sci_abstract_list_get_next(element);
   }

   return SCI_INVALID_HANDLE;
}

// ---------------------------------------------------------------------------

#if !defined(DISABLE_SCI_ITERATORS)

SCI_ITERATOR_HANDLE_T scif_domain_get_remote_device_iterator(
   SCI_DOMAIN_HANDLE_T   domain,
   void                * iterator_buffer
)
{
   SCI_ITERATOR_HANDLE_T iterator = (SCI_ITERATOR_HANDLE_T *)iterator_buffer;

   sci_base_iterator_construct(
      iterator, &((SCIF_SAS_DOMAIN_T*) domain)->remote_device_list
   );


   return iterator;
}

#endif // !defined(DISABLE_SCI_ITERATORS)

// ---------------------------------------------------------------------------

SCI_STATUS scif_domain_discover(
   SCI_DOMAIN_HANDLE_T   domain,
   U32                   discover_timeout,
   U32                   device_timeout
)
{
   SCIF_SAS_DOMAIN_T * fw_domain = (SCIF_SAS_DOMAIN_T*) domain;
   SCI_STATUS          status    = SCI_SUCCESS;
   SCI_STATUS          op_status = SCI_SUCCESS;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_domain_discover(0x%x, 0x%x, 0x%x) enter\n",
      domain, discover_timeout, device_timeout
   ));

   // Check to make sure the size of the domain doesn't cause potential issues
   // with the remote device timer and the domain timer.
   if ((device_timeout * sci_abstract_list_size(&fw_domain->remote_device_list))
        > discover_timeout)
      status = SCI_WARNING_TIMER_CONFLICT;

   op_status = fw_domain->state_handlers->discover_handler(
                  &fw_domain->parent, discover_timeout, device_timeout
               );

   // The status of the discover operation takes priority.
   if (  (status == SCI_SUCCESS)
      || (status != SCI_SUCCESS && op_status != SCI_SUCCESS) )
   {
      status = op_status;
   }

   return status;
}

// ---------------------------------------------------------------------------

U32 scif_domain_get_suggested_discover_timeout(
   SCI_DOMAIN_HANDLE_T   domain
)
{
   U32 suggested_timeout = SCIF_DOMAIN_DISCOVER_TIMEOUT; //milli-seconds
   return suggested_timeout;
}

// ---------------------------------------------------------------------------

void scic_cb_port_stop_complete(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCI_PORT_HANDLE_T        port,
   SCI_STATUS               completion_status
)
{
   SCIF_LOG_TRACE((
      sci_base_object_get_logger((SCIF_SAS_DOMAIN_T*)sci_object_get_association(port)),
      SCIF_LOG_OBJECT_DOMAIN,
      "scic_cb_port_stop_complete(0x%x, 0x%x, 0x%x) enter\n",
      controller, port, completion_status
   ));
}

// ---------------------------------------------------------------------------

void scic_cb_port_ready(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCI_PORT_HANDLE_T        port
)
{
   SCIF_SAS_DOMAIN_T * fw_domain = (SCIF_SAS_DOMAIN_T*)
                                   sci_object_get_association(port);

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_domain),
      SCIF_LOG_OBJECT_DOMAIN,
      "scic_cb_port_ready(0x%x, 0x%x) enter\n",
      controller, port
   ));

   // The controller supplied with the port should match the controller
   // saved in the domain.
   ASSERT(sci_object_get_association(controller) == fw_domain->controller);

   fw_domain->is_port_ready = TRUE;

   fw_domain->state_handlers->port_ready_handler(&fw_domain->parent);
}

// ---------------------------------------------------------------------------

void scic_cb_port_not_ready(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCI_PORT_HANDLE_T        port,
   U32                      reason_code
)
{
   SCIF_SAS_DOMAIN_T * fw_domain = (SCIF_SAS_DOMAIN_T*)
                                   sci_object_get_association(port);

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_domain),
      SCIF_LOG_OBJECT_DOMAIN,
      "scic_cb_port_not_ready(0x%x, 0x%x) enter\n",
      controller, port
   ));

   // The controller supplied with the port should match the controller
   // saved in the domain.
   ASSERT(sci_object_get_association(controller) == fw_domain->controller);

   // There is no need to take action on the port reconfiguring since it is
   // just a change of the port width.
   if (reason_code != SCIC_PORT_NOT_READY_RECONFIGURING)
   {
      fw_domain->is_port_ready = FALSE;

      fw_domain->state_handlers->port_not_ready_handler(
                                    &fw_domain->parent, reason_code);
   }
}

// ---------------------------------------------------------------------------

void scic_cb_port_hard_reset_complete(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCI_PORT_HANDLE_T        port,
   SCI_STATUS               completion_status
)
{
   SCIF_SAS_DOMAIN_T        * fw_domain = (SCIF_SAS_DOMAIN_T*)
                                   sci_object_get_association(port);
   SCIF_SAS_REMOTE_DEVICE_T * fw_device;
   SCI_FAST_LIST_ELEMENT_T  * element = fw_domain->request_list.list_head;
   SCIF_SAS_TASK_REQUEST_T  * task_request = NULL;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_domain),
      SCIF_LOG_OBJECT_DOMAIN,
      "scic_cb_port_hard_reset_complete(0x%x, 0x%x, 0x%x) enter\n",
      controller, port, completion_status
   ));

   while (element != NULL)
   {
      task_request = (SCIF_SAS_TASK_REQUEST_T*) sci_fast_list_get_object(element);
      element = sci_fast_list_get_next(element);

      if (scif_sas_task_request_get_function(task_request)
             == SCI_SAS_HARD_RESET)
      {
         fw_device = task_request->parent.device;

         if (fw_device->domain == fw_domain)
         {
            scic_remote_device_reset_complete(fw_device->core_object);

            scif_cb_task_request_complete(
               sci_object_get_association(controller),
               fw_device,
               task_request,
               (SCI_TASK_STATUS) completion_status
            );

            break;
         }
      }
   }
}

// ---------------------------------------------------------------------------

void scic_cb_port_bc_change_primitive_recieved(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCI_PORT_HANDLE_T        port,
   SCI_PHY_HANDLE_T         phy
)
{
   SCIF_SAS_DOMAIN_T * fw_domain = (SCIF_SAS_DOMAIN_T*)
                                   sci_object_get_association(port);

   SCIF_SAS_CONTROLLER_T * fw_controller = (SCIF_SAS_CONTROLLER_T *)
                                           sci_object_get_association(controller);

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scic_cb_port_bc_change_primitive_recieved(0x%x, 0x%x, 0x%x) enter\n",
      controller, port, phy
   ));

   if (fw_domain->broadcast_change_count == 0)
   {  // Enable the BCN detection only if the bcn_count is zero. If bcn_count is
      // not zero at this time, we won't enable BCN detection since all non-zero
      // BCN_count means same to us. Furthermore, we avoid BCN storm by not
      // always enabling the BCN_detection.
      scic_port_enable_broadcast_change_notification(fw_domain->core_object);
   }

   fw_domain->broadcast_change_count++;

   //if there is smp device on this domain that is in the middle of discover
   //process or smp target reset, don't notify the driver layer.
   if( ! scif_sas_domain_is_in_smp_activity(fw_domain) )
      // Notify the user that there is, potentially, a change to the domain.
      scif_cb_domain_change_notification(fw_controller, fw_domain);
}

// ---------------------------------------------------------------------------

void scic_cb_port_bc_ses_primitive_recieved(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCI_PORT_HANDLE_T        port,
   SCI_PHY_HANDLE_T         phy
)
{
   SCIF_LOG_TRACE((
      sci_base_object_get_logger(sci_object_get_association(port)),
      SCIF_LOG_OBJECT_DOMAIN,
      "scic_cb_port_bc_ses_primitive_received(0x%x, 0x%x, 0x%x) enter\n",
      controller, port, phy
   ));
}

// ---------------------------------------------------------------------------

void scic_cb_port_bc_expander_primitive_recieved(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCI_PORT_HANDLE_T        port,
   SCI_PHY_HANDLE_T         phy
)
{
   SCIF_LOG_TRACE((
      sci_base_object_get_logger(sci_object_get_association(port)),
      SCIF_LOG_OBJECT_DOMAIN,
      "scic_cb_port_bc_expander_primitive_received(0x%x, 0x%x, 0x%x) enter\n",
      controller, port, phy
   ));
}

// ---------------------------------------------------------------------------

void scic_cb_port_bc_aen_primitive_recieved(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCI_PORT_HANDLE_T        port,
   SCI_PHY_HANDLE_T         phy
)
{
   SCIF_LOG_TRACE((
      sci_base_object_get_logger(sci_object_get_association(port)),
      SCIF_LOG_OBJECT_DOMAIN,
      "scic_cb_port_bc_aen_primitive_received(0x%x, 0x%x, 0x%x) enter\n",
      controller, port, phy
   ));
}

// ---------------------------------------------------------------------------

void scic_cb_port_link_up(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCI_PORT_HANDLE_T        port,
   SCI_PHY_HANDLE_T         phy
)
{
   SCIF_SAS_DOMAIN_T        * fw_domain = (SCIF_SAS_DOMAIN_T*)
                                 sci_object_get_association(port);

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(sci_object_get_association(port)),
      SCIF_LOG_OBJECT_DOMAIN,
      "scic_cb_port_link_up(0x%x, 0x%x, 0x%x) enter\n",
      controller, port, phy
   ));

   scif_sas_domain_update_device_port_width(fw_domain, port);
}

// ---------------------------------------------------------------------------

void scic_cb_port_link_down(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCI_PORT_HANDLE_T        port,
   SCI_PHY_HANDLE_T         phy
)
{
   SCIF_SAS_DOMAIN_T        * fw_domain = (SCIF_SAS_DOMAIN_T*)
                                 sci_object_get_association(port);

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(sci_object_get_association(port)),
      SCIF_LOG_OBJECT_DOMAIN,
      "scic_cb_port_link_down(0x%x, 0x%x, 0x%x) enter\n",
      controller, port, phy
   ));

   scif_sas_domain_update_device_port_width(fw_domain, port);
}

//******************************************************************************
//* P R O T E C T E D   M E T H O D S
//******************************************************************************

/**
 * @brief This method constructs the framework's SAS domain object.  During
 *        the construction process a linkage to the corresponding core port
 *        object.
 *
 * @param[in]  domain This parameter specifies the domain object to be
 *             constructed.
 * @param[in]  domain_id This parameter specifies the ID for the domain
 *             object.
 * @param[in]  fw_controller This parameter specifies the controller managing
 *             the domain being constructed.
 *
 * @return none
 */
void scif_sas_domain_construct(
   SCIF_SAS_DOMAIN_T     * fw_domain,
   U8                      domain_id,
   SCIF_SAS_CONTROLLER_T * fw_controller
)
{
   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_controller),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_INITIALIZATION,
      "scif_sas_domain_construct(0x%x, 0x%x, 0x%x) enter\n",
      fw_domain, domain_id, fw_controller
   ));

   sci_base_domain_construct(
      &fw_domain->parent,
      sci_base_object_get_logger(fw_controller),
      scif_sas_domain_state_table
   );

   scif_sas_domain_initialize_state_logging(fw_domain);

   sci_abstract_list_construct(
      &fw_domain->remote_device_list, &fw_controller->free_remote_device_pool
   );

   // Retrieve the core's port object that directly corresponds to this
   // domain.
   scic_controller_get_port_handle(
      fw_controller->core_object, domain_id, &fw_domain->core_object
   );

   // Set the association in the core port to this framework domain object.
   sci_object_set_association(
      (SCI_OBJECT_HANDLE_T) fw_domain->core_object, fw_domain
   );

   sci_fast_list_init(&fw_domain->request_list);

   fw_domain->operation.timer = NULL;

   fw_domain->is_port_ready      = FALSE;
   fw_domain->device_start_count = 0;
   fw_domain->controller         = fw_controller;
   fw_domain->operation.status   = SCI_SUCCESS;
   fw_domain->is_config_route_table_needed = FALSE;
}

/**
 * @brief This method will terminate the requests outstanding in the core
 *        based on the supplied criteria.
 *        - if the all three parameters are specified then only the single
 *          SCIF_SAS_REQUEST object is terminated.
 *        - if only the SCIF_SAS_DOMAIN and SCIF_SAS_REMOTE_DEVICE are
 *          specified, then all SCIF_SAS_REQUEST objects outstanding at
 *          the device are terminated.  The one exclusion to this rule is
 *          that the fw_requestor is not terminated.
 *        - if only the SCIF_SAS_DOMAIN object is specified, then all
 *          SCIF_SAS_REQUEST objects outstanding in the domain are
 *          terminated.
 *
 * @param[in]  fw_domain This parameter specifies the domain in which to
 *             terminate requests.
 * @param[in]  fw_device This parameter specifies the remote device in
 *             which to terminate requests.  This parameter can be NULL
 *             as long as the fw_request parameter is NULL.  It is a
 *             required parameter if the fw_request parameter is not NULL.
 * @param[in]  fw_request This parameter specifies the request object to
 *             be terminated.  This parameter can be NULL.
 * @param[in]  fw_requestor This parameter specifies the task management
 *             request that is responsible for the termination of requests.
 *
 * @return none
 */
void scif_sas_domain_terminate_requests(
   SCIF_SAS_DOMAIN_T        * fw_domain,
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   SCIF_SAS_REQUEST_T       * fw_request,
   SCIF_SAS_TASK_REQUEST_T  * fw_requestor
)
{
   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_TASK_MANAGEMENT,
      "scif_sas_domain_terminate_requests(0x%x, 0x%x, 0x%x, 0x%x) enter\n",
      fw_domain, fw_device, fw_request, fw_requestor
   ));

   if (fw_request != NULL)
   {
      fw_request->terminate_requestor = fw_requestor;
      fw_request->state_handlers->abort_handler(&fw_request->parent);
   }
   else
   {
      SCI_FAST_LIST_ELEMENT_T * element = fw_domain->request_list.list_head;
      SCIF_SAS_REQUEST_T      * request = NULL;

      // Cycle through the fast list of IO requests.  Terminate each
      // outstanding requests that matches the criteria supplied by the
      // caller.
      while (element != NULL)
      {
         request = (SCIF_SAS_REQUEST_T*) sci_fast_list_get_object(element);
         // The current element may be deleted from the list because of
         // IO completion so advance to the next element early
         element = sci_fast_list_get_next(element);

         // Ensure we pass the supplied criteria before terminating the
         // request.
         if (
               (fw_device == NULL)
            || (
                  (request->device == fw_device)
               && (fw_requestor != (SCIF_SAS_TASK_REQUEST_T*) request)
               )
            )
         {
            if (
                  (request->is_waiting_for_abort_task_set == FALSE) ||
                  (request->terminate_requestor == NULL)
               )
            {
               request->terminate_requestor = fw_requestor;
               request->state_handlers->abort_handler(&request->parent);
            }
         }
      }
   }
}

/**
 * @brief This method searches the domain object to find a
 *        SCIF_SAS_REQUEST object associated with the supplied IO tag.
 *
 * @param[in]  fw_domain This parameter specifies the domain in which to
 *             to find the request object.
 * @param[in]  io_tag This parameter specifies the IO tag value for which
 *             to locate the corresponding request.
 *
 * @return This method returns a pointer to the SCIF_SAS_REQUEST object
 *         associated with the supplied IO tag.
 * @retval NULL This value is returned if the IO tag does not resolve to
 *         a request.
 */
SCIF_SAS_REQUEST_T * scif_sas_domain_get_request_by_io_tag(
   SCIF_SAS_DOMAIN_T * fw_domain,
   U16                 io_tag
)
{
   SCI_FAST_LIST_ELEMENT_T * element    = fw_domain->request_list.list_head;
   SCIF_SAS_IO_REQUEST_T   * io_request = NULL;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_TASK_MANAGEMENT,
      "scif_sas_domain_get_request_by_io_tag(0x%x, 0x%x) enter\n",
      fw_domain, io_tag
   ));

   while (element != NULL)
   {
      io_request = (SCIF_SAS_IO_REQUEST_T*) sci_fast_list_get_object(element);

      // Check to see if we located the request with an identical IO tag.
      if (scic_io_request_get_io_tag(io_request->parent.core_object) == io_tag)
         return &io_request->parent;

      element = sci_fast_list_get_next(element);
   }

   return NULL;
}

/**
 * @brief This method performs domain object initialization to be done
 *        when the scif_controller_initialize() method is invoked.
 *        This includes operation timeout creation.
 *
 * @param[in]  fw_domain This parameter specifies the domain object for
 *             which to perform initialization.
 *
 * @return none
 */
void scif_sas_domain_initialize(
   SCIF_SAS_DOMAIN_T * fw_domain
)
{
   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_INITIALIZATION,
      "scif_sas_domain_initialize(0x%x) enter\n",
      fw_domain
   ));

   // Create the timer for each domain.  It is too early in the process
   // to allocate this during construction since the user didn't have
   // a chance to set it's association.
   if (fw_domain->operation.timer == 0)
   {
      fw_domain->operation.timer = scif_cb_timer_create(
                                      fw_domain->controller,
                                      scif_sas_domain_operation_timeout_handler,
                                      fw_domain
                                   );
   }
}

/**
 * @brief This method performs domain object handling for core remote
 *        device start complete notifications.  Core remote device starts
 *        and start completes are only done during discovery.  This could
 *        ultimately be wrapped into a handler method on the domain (they
 *        actually already exist).  This method will decrement the number
 *        of device start operations ongoing and attempt to determine if
 *        discovery is complete.
 *
 * @param[in]  fw_domain This parameter specifies the domain object for
 *             which to perform initialization.
 *
 * @return none
 */
void scif_sas_domain_remote_device_start_complete(
   SCIF_SAS_DOMAIN_T        * fw_domain,
   SCIF_SAS_REMOTE_DEVICE_T * fw_device
)
{
   SMP_DISCOVER_RESPONSE_PROTOCOLS_T  dev_protocols;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_domain_remote_device_start_complete(0x%x, 0x%x) enter\n",
      fw_domain, fw_device
   ));

   // If a device is being started/start completed, then we must be
   // during discovery.
   ASSERT(fw_domain->parent.state_machine.current_state_id
          == SCI_BASE_DOMAIN_STATE_DISCOVERING);

   scic_remote_device_get_protocols(fw_device->core_object, &dev_protocols);

   // Decrement the number of devices being started and check to see
   // if all have finished being started or failed as the case may be.
   fw_domain->device_start_in_progress_count--;

   if ( dev_protocols.u.bits.attached_smp_target )
   {
      if ( fw_device->containing_device == NULL )
         //kick off the smp discover process if this expander is direct attached.
         scif_sas_smp_remote_device_start_discover(fw_device);
      else
         //mark this device, the discover process of this device will start after
         //its containing smp device finish discover.
         fw_device->protocol_device.smp_device.scheduled_activity =
            SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_DISCOVER;
   }
   else
   {
      fw_domain->state_handlers->device_start_complete_handler(
         &fw_domain->parent, &fw_device->parent
      );
   }
}


/**
 * @brief This methods check each smp device in this domain. If there is at
 *        least one smp device in discover or target reset activity, this
 *        domain is considered in smp activity. Note this routine is not
 *        called on fast IO path.
 *
 * @param[in] fw_domain The framework domain object
 *
 * @return BOOL value to indicate whether a domain is in SMP activity.
 */
BOOL scif_sas_domain_is_in_smp_activity(
   SCIF_SAS_DOMAIN_T        * fw_domain
)
{
   SCI_ABSTRACT_ELEMENT_T * current_element =
      sci_abstract_list_get_front(&fw_domain->remote_device_list);

   SCIF_SAS_REMOTE_DEVICE_T * current_device;

   while ( current_element != NULL )
   {
      SMP_DISCOVER_RESPONSE_PROTOCOLS_T  dev_protocols;

      current_device = (SCIF_SAS_REMOTE_DEVICE_T *)
                       sci_abstract_list_get_object(current_element);

      scic_remote_device_get_protocols(current_device->core_object,
                                       &dev_protocols
      );

      if (dev_protocols.u.bits.attached_smp_target &&
          scif_sas_smp_remote_device_is_in_activity(current_device))
         return TRUE;

      current_element =
         sci_abstract_list_get_next(current_element);
   }

   return FALSE;
}


/**
 * @brief This methods finds a expander attached device by searching the domain's
 *        device list using connected expander device and expander phy id.
 *
 * @param[in] fw_domain The framework domain object
 * @param[in] parent_device The expander device the target device attaches to.
 * @param[in] expander_phy_id The expander phy id that the target device owns.
 *
 * @return found remote device or a NULL value if no device found.
 */
SCIF_SAS_REMOTE_DEVICE_T * scif_sas_domain_get_device_by_containing_device(
   SCIF_SAS_DOMAIN_T        * fw_domain,
   SCIF_SAS_REMOTE_DEVICE_T * containing_device,
   U8                         expander_phy_id
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device;
   SCI_ABSTRACT_ELEMENT_T * element = sci_abstract_list_get_front(
                                         &fw_domain->remote_device_list
                                      );

   //parent device must not be NULL.
   ASSERT(containing_device != NULL);

   // Search the abstract list to see if there is a remote device meets the
   // search condition.
   while (element != NULL)
   {
      fw_device = (SCIF_SAS_REMOTE_DEVICE_T*)
                  sci_abstract_list_get_object(element);

      // Check to see if this is the device for which we are searching.
      if (
            (fw_device->containing_device == containing_device)
         && (fw_device->expander_phy_identifier == expander_phy_id)
         )
      {
         return fw_device;
      }

      element = sci_abstract_list_get_next(element);
   }

   return SCI_INVALID_HANDLE;
}


/**
 * @brief This methods finds the first device that is in STOPPED state and its
 *        connection_rate is still in SPINUP_HOLD(value 3).
 *
 * @param[in] fw_domain The framework domain object
 *
 * @return SCIF_SAS_REMOTE_DEVICE_T The device that is in SPINUP_HOLD or NULL.
 */
SCIF_SAS_REMOTE_DEVICE_T * scif_sas_domain_find_device_in_spinup_hold(
   SCIF_SAS_DOMAIN_T        * fw_domain
)
{
   SCI_ABSTRACT_ELEMENT_T   * current_element;
   SCIF_SAS_REMOTE_DEVICE_T * current_device;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_domain),
      SCIF_LOG_OBJECT_DOMAIN,
      "scif_sas_domain_find_device_in_spinup_hold(0x%x) enter\n",
      fw_domain
   ));

   //search throught domain's device list to find the first sata device on spinup_hold
   current_element = sci_abstract_list_get_front(&fw_domain->remote_device_list);
   while (current_element != NULL )
   {
      current_device = (SCIF_SAS_REMOTE_DEVICE_T *)
                       sci_abstract_list_get_object(current_element);

      //We must get the next element before we remove the current
      //device. Or else, we will get wrong next_element, since the erased
      //element has been put into free pool.
      current_element = sci_abstract_list_get_next(current_element);

      if ( sci_base_state_machine_get_state(&current_device->parent.state_machine) ==
              SCI_BASE_REMOTE_DEVICE_STATE_STOPPED
          && scic_remote_device_get_connection_rate(current_device->core_object) ==
                SCI_SATA_SPINUP_HOLD )
      {
         return current_device;
      }
   }

   return NULL;
}


/**
 * @brief This methods finds the first device that has specific activity scheduled.
 *
 * @param[in] fw_domain The framework domain object
 * @param[in] smp_activity A specified smp activity. The valid range is [1,5].
 *
 * @return SCIF_SAS_REMOTE_DEVICE_T The device that has specified smp activity scheduled.
 */
SCIF_SAS_REMOTE_DEVICE_T * scif_sas_domain_find_device_has_scheduled_activity(
   SCIF_SAS_DOMAIN_T        * fw_domain,
   U8                         smp_activity
)
{
   SCI_ABSTRACT_ELEMENT_T * current_element =
      sci_abstract_list_get_front(&fw_domain->remote_device_list);

   SCIF_SAS_REMOTE_DEVICE_T * current_device;
   SMP_DISCOVER_RESPONSE_PROTOCOLS_T  dev_protocols;

   //config route table activity has higher priority than discover activity.
   while ( current_element != NULL )
   {
      current_device = (SCIF_SAS_REMOTE_DEVICE_T *)
                       sci_abstract_list_get_object(current_element);

      scic_remote_device_get_protocols(current_device->core_object,
                                       &dev_protocols);

      current_element =
         sci_abstract_list_get_next(current_element);

      if ( dev_protocols.u.bits.attached_smp_target
          && current_device->protocol_device.smp_device.scheduled_activity ==
                smp_activity)
      {
         return current_device;
      }
   }

   return NULL;
}


/**
 * @brief This methods finds the smp device that has is_config_route_table_scheduled
 *        flag set to TRUE, and start config route table on it. If there is no
 *        smp device scheduled to config route table, find the smp device has
 *        is_discover_scheduled and start the smp discover process on them.
 *
 * @param[in] fw_domain The framework domain that to start smp discover process.
 *
 * @return NONE
 */
void scif_sas_domain_start_smp_activity(
  SCIF_SAS_DOMAIN_T        * fw_domain
)
{
   SCIF_SAS_REMOTE_DEVICE_T * device_has_scheduled_activity = NULL;

   //first, find device that has config route table activity scheduled.
   //config route table activity has higher priority than Discover.
   device_has_scheduled_activity =
      scif_sas_domain_find_device_has_scheduled_activity(
         fw_domain,
         SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_CONFIG_ROUTE_TABLE
      );

   if (device_has_scheduled_activity != NULL)
   {
      scif_sas_smp_remote_device_configure_route_table(device_has_scheduled_activity);
      device_has_scheduled_activity->protocol_device.smp_device.scheduled_activity =
         SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_NONE;
      return;
   }

   //if no device has config route table activity scheduled, search again, find
   //device has discover activity scheduled.
   device_has_scheduled_activity =
      scif_sas_domain_find_device_has_scheduled_activity(
         fw_domain,
         SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_DISCOVER
      );

   if (device_has_scheduled_activity != NULL)
      scif_sas_smp_remote_device_start_discover(device_has_scheduled_activity);
}


/**
 * @brief This method starts domain's smp discover process from the top level expander.
 *
 * @param[in] fw_domain The framework domain that to start smp discover process.
 @ @param[in] top_expander The top level expander device to start smp discover process.
 *
 * @return None
 */
void scif_sas_domain_start_smp_discover(
   SCIF_SAS_DOMAIN_T        * fw_domain,
   SCIF_SAS_REMOTE_DEVICE_T * top_expander
)
{
   SCI_ABSTRACT_ELEMENT_T * current_element =
       sci_abstract_list_get_front(&fw_domain->remote_device_list);

   SCIF_SAS_REMOTE_DEVICE_T * current_device;

   // something changed behind expander
   // mark all the device behind expander to be NOT
   // is_currently_discovered.
   while ( current_element != NULL )
   {
      current_device = (SCIF_SAS_REMOTE_DEVICE_T *)
                           sci_abstract_list_get_object(current_element);

      current_device->is_currently_discovered = FALSE;

      //reset all the devices' port witdh except the top expander.
      if (current_device->containing_device != NULL)
         current_device->device_port_width = 1;

      current_element = sci_abstract_list_get_next(current_element);
   }

   //expander device itself should be set to is_currently_discovered.
   top_expander->is_currently_discovered = TRUE;

   //kick off the smp discover process.
   scif_sas_smp_remote_device_start_discover(top_expander);
}


/**
 * @brief This method continues domain's smp discover process and
 *        may transit to READY state if all smp activities are done.
 *
 * @param[in] fw_domain The framework domain that to start smp discover process.
 *
 * @return None
 */
void scif_sas_domain_continue_discover(
   SCIF_SAS_DOMAIN_T * fw_domain
)
{
   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_domain_continue_discover(0x%x) enter\n",
      fw_domain
   ));

   if ( fw_domain->device_start_in_progress_count == 0
       && !scif_sas_domain_is_in_smp_activity(fw_domain) )
   {
      //domain scrub the remote device list to see if there is a need
      //to start smp discover on expander device. There may be no
      //need to start any smp discover.
      scif_sas_domain_start_smp_activity(fw_domain);

      //In domain discovery timeout case, we cancel all
      //the smp activities, and terminate all the smp requests, then
      //this routine is called. But the smp request may not done
      //terminated. We want to guard the domain trasitting to READY
      //by checking outstanding smp request count. If there is outstanding
      //smp request, the domain will not transit to READY. Later when
      //the smp request is terminated at smp remote device, this routine
      //will be called then the domain will transit to READY state.
      if ( ! scif_sas_domain_is_in_smp_activity(fw_domain)
          && scif_sas_domain_get_smp_request_count(fw_domain) == 0)
      {
         //before domain transit to READY state, domain has some clean up
         //work to do, such like update domain's remote devcie list.
         scif_sas_domain_finish_discover(fw_domain);
      }
   }
}


/**
 * @brief This method finishes domain's smp discover process and
 *        update domain's remote device list.
 *
 * @param[in] fw_domain The framework domain that's to finish smp discover process.
 *
 * @return None
 */
void scif_sas_domain_finish_discover(
   SCIF_SAS_DOMAIN_T * fw_domain
)
{
   SCIF_SAS_REMOTE_DEVICE_T * current_device = NULL;
   SCI_ABSTRACT_ELEMENT_T   * current_element = NULL;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_domain_finish_discover(0x%x) enter\n",
      fw_domain
   ));

   //need to scrub all the devices behind the expander. Check each
   //device's discover_status. if the is_currently_discovered is FALSE, means
   //the device is not been rediscovered. this device needs to be removed.
   current_element = sci_abstract_list_get_front(&fw_domain->remote_device_list);
   while (current_element != NULL )
   {
      current_device = (SCIF_SAS_REMOTE_DEVICE_T *)
                          sci_abstract_list_get_object(current_element);

      //We must get the next element before we remove the current
      //device. Or else, we will get wrong next_element, since the erased
      //element has been put into free pool.
      current_element = sci_abstract_list_get_next(current_element);

      if ( current_device->is_currently_discovered == FALSE )
      {
         // Notify the framework user of the device removal.
         scif_cb_domain_device_removed(
            fw_domain->controller, fw_domain, current_device
         );
      }
   }

   sci_base_state_machine_change_state(
      &fw_domain->parent.state_machine, SCI_BASE_DOMAIN_STATE_READY
   );
}



/**
 * @brief This method remove an expander device and its child devices, in order to
 *        deal with a detected illeagal phy connection.
 *
 * @param[in] fw_domain The domain that a expander belongs to.
 * @param[in] fw_device The expander device to be removed.
 *
 * @return none.
 */
void scif_sas_domain_remove_expander_device(
   SCIF_SAS_DOMAIN_T        * fw_domain,
   SCIF_SAS_REMOTE_DEVICE_T * fw_device
)
{
   SCIF_SAS_SMP_REMOTE_DEVICE_T * smp_remote_device =
      &fw_device->protocol_device.smp_device;

   SCI_FAST_LIST_ELEMENT_T     * element = smp_remote_device->smp_phy_list.list_head;
   SCIF_SAS_SMP_PHY_T          * curr_smp_phy = NULL;
   SCIF_SAS_REMOTE_DEVICE_T    * current_device = NULL;

   while (element != NULL)
   {
      curr_smp_phy = (SCIF_SAS_SMP_PHY_T*) sci_fast_list_get_object(element);
      element = sci_fast_list_get_next(element);

      if ( curr_smp_phy->attached_device_type != SMP_NO_DEVICE_ATTACHED
          && curr_smp_phy->u.end_device != NULL )
      {
         if (curr_smp_phy->attached_device_type == SMP_END_DEVICE_ONLY)
            current_device = curr_smp_phy->u.end_device;
         else
            current_device = curr_smp_phy->u.attached_phy->owning_device;

         scif_cb_domain_device_removed(fw_domain->controller, fw_domain, current_device);
      }
   }

   //remove device itself
   scif_cb_domain_device_removed(fw_domain->controller, fw_domain, fw_device);
}


/**
 * @brief This method searches the whole domain and finds all the smp devices to
 *        cancel their smp activities if there is any.
 *
 * @param[in] fw_domain The domain that its smp activities are to be canceled.
 *
 * @return none.
 */
void scif_sas_domain_cancel_smp_activities(
   SCIF_SAS_DOMAIN_T * fw_domain
)
{
   SCI_ABSTRACT_ELEMENT_T * current_element =
      sci_abstract_list_get_front(&fw_domain->remote_device_list);

   SCIF_SAS_REMOTE_DEVICE_T * current_device;

   //purge all the outstanding internal IOs in HPQ.
   scif_sas_high_priority_request_queue_purge_domain(
      &fw_domain->controller->hprq, fw_domain
   );

   while ( current_element != NULL )
   {
      SMP_DISCOVER_RESPONSE_PROTOCOLS_T  dev_protocols;

      current_device = (SCIF_SAS_REMOTE_DEVICE_T *)
                       sci_abstract_list_get_object(current_element);

      scic_remote_device_get_protocols(current_device->core_object,
                                       &dev_protocols
      );

      if (dev_protocols.u.bits.attached_smp_target)
      {
         scif_sas_smp_remote_device_cancel_smp_activity(current_device);
      }

      current_element =
         sci_abstract_list_get_next(current_element);
   }
}


/**
 * @brief This method searches the domain's request list and counts outstanding
 *           smp IOs.
 *
 * @param[in] fw_domain The domain that its request list is to be searched.
 *
 * @return U8 The possible return value of this routine is 0 or 1.
 */
U8 scif_sas_domain_get_smp_request_count(
   SCIF_SAS_DOMAIN_T * fw_domain
)
{
   SCI_FAST_LIST_ELEMENT_T * element = fw_domain->request_list.list_head;
   SCIF_SAS_REQUEST_T      * request = NULL;
   U8                        count = 0;
   SCIC_TRANSPORT_PROTOCOL   protocol;

   // Cycle through the fast list of IO requests.  Terminate each
   // outstanding requests that matches the criteria supplied by the
   // caller.
   while (element != NULL)
   {
      request = (SCIF_SAS_REQUEST_T*) sci_fast_list_get_object(element);
      // The current element may be deleted from the list because of
      // IO completion so advance to the next element early
      element = sci_fast_list_get_next(element);

      protocol = scic_io_request_get_protocol(request->core_object);

      if ( protocol == SCIC_SMP_PROTOCOL)
         count++;
   }

   return count;
}


/**
 * @brief This method start clear affiliation activities for smp devices in
 *           this domain.
 *
 * @param[in] fw_domain The domain that its smp devices are scheduled to clear
 *                affiliation for all the EA SATA devices.
 *
 * @return none.
 */
void scif_sas_domain_start_clear_affiliation(
   SCIF_SAS_DOMAIN_T * fw_domain
)
{
   scif_sas_domain_schedule_clear_affiliation(fw_domain);
   scif_sas_domain_continue_clear_affiliation(fw_domain);
}


/**
 * @brief This method schedule clear affiliation activities for smp devices in
 *           this domain.
 *
 * @param[in] fw_domain The domain that its smp devices are scheduled to clear
 *                affiliation for all the EA SATA devices.
 *
 * @return none.
 */
void scif_sas_domain_schedule_clear_affiliation(
   SCIF_SAS_DOMAIN_T * fw_domain
)
{
   SCI_ABSTRACT_ELEMENT_T * current_element =
      sci_abstract_list_get_front(&fw_domain->remote_device_list);

   SCIF_SAS_REMOTE_DEVICE_T * current_device;
   SMP_DISCOVER_RESPONSE_PROTOCOLS_T  dev_protocols;

   //config route table activity has higher priority than discover activity.
   while ( current_element != NULL )
   {
      current_device = (SCIF_SAS_REMOTE_DEVICE_T *)
                       sci_abstract_list_get_object(current_element);

      scic_remote_device_get_protocols(current_device->core_object,
                                       &dev_protocols);

      current_element =
         sci_abstract_list_get_next(current_element);

      if ( dev_protocols.u.bits.attached_smp_target )
      {
         current_device->protocol_device.smp_device.scheduled_activity =
            SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_CLEAR_AFFILIATION;
      }
   }
}


/**
 * @brief This method carries clear affiliation activities for a smp devices in
 *           this domain during controller stop process.
 *
 * @param[in] fw_domain The domain that its smp devices are to clear
 *                affiliation for all the EA SATA devices.
 *
 * @return none.
 */
void scif_sas_domain_continue_clear_affiliation(
   SCIF_SAS_DOMAIN_T * fw_domain
)
{
   SCIF_SAS_REMOTE_DEVICE_T * smp_device =
      scif_sas_domain_find_device_has_scheduled_activity(
         fw_domain,
         SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_CLEAR_AFFILIATION
      );

   if (smp_device != NULL)
      scif_sas_smp_remote_device_start_clear_affiliation(smp_device);
   else
   {
      //This domain has done clear affiliation.
      SCIF_SAS_CONTROLLER_T * fw_controller = fw_domain->controller;
      fw_controller->current_domain_to_clear_affiliation++;

      //let controller continue to clear affiliation on other domains.
      scif_sas_controller_clear_affiliation(fw_domain->controller);
   }
}


/**
 * @brief This method releases resource for a framework domain.
 *
 * @param[in] fw_controller This parameter specifies the framework
 *            controller, its associated domain's resources are to be released.
 * @param[in] fw_domain This parameter specifies the framework
 *            domain whose resources are to be released.
 */
void scif_sas_domain_release_resource(
   SCIF_SAS_CONTROLLER_T * fw_controller,
   SCIF_SAS_DOMAIN_T     * fw_domain
)
{
   if (fw_domain->operation.timer != NULL)
   {
      scif_cb_timer_destroy(fw_controller, fw_domain->operation.timer);
      fw_domain->operation.timer = NULL;
   }
}


/**
 * @brief This method finds the a EA device that has target reset scheduled.
 *
 * @param[in] fw_domain The framework domain object
 *
 * @return SCIF_SAS_REMOTE_DEVICE_T The EA device that has target reset scheduled.
 */
SCIF_SAS_REMOTE_DEVICE_T * scif_sas_domain_find_next_ea_target_reset(
   SCIF_SAS_DOMAIN_T     * fw_domain
)
{
   SCI_ABSTRACT_ELEMENT_T   * current_element;
   SCIF_SAS_REMOTE_DEVICE_T * current_device;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_domain),
      SCIF_LOG_OBJECT_DOMAIN,
      "scif_sas_domain_find_next_ea_target_reset(0x%x) enter\n",
      fw_domain
   ));

   //search through domain's device list to find the first sata device on spinup_hold
   current_element = sci_abstract_list_get_front(&fw_domain->remote_device_list);
   while (current_element != NULL )
   {
      current_device = (SCIF_SAS_REMOTE_DEVICE_T *)
                       sci_abstract_list_get_object(current_element);

      current_element = sci_abstract_list_get_next(current_element);

      if ( current_device->ea_target_reset_request_scheduled != NULL )
      {
         return current_device;
      }
   }

   return NULL;
}

#if !defined(DISABLE_WIDE_PORTED_TARGETS)
/**
 * @brief This method update the direct attached device port width.
 *
 * @param[in] fw_domain The framework domain object
 * @param[in] port The associated port object which recently has link up/down
 *                 event happened.
 *
 * @return none
 */
void scif_sas_domain_update_device_port_width(
   SCIF_SAS_DOMAIN_T * fw_domain,
   SCI_PORT_HANDLE_T   port
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device;
   SCIC_PORT_PROPERTIES_T     properties;
   U8                         new_port_width = 0;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_domain),
      SCIF_LOG_OBJECT_DOMAIN,
      "scif_sas_domain_update_device_port_width(0x%x, 0x%x) enter\n",
      fw_domain, port
   ));

   scic_port_get_properties(port, &properties);

   fw_device = (SCIF_SAS_REMOTE_DEVICE_T *)
                  scif_domain_get_device_by_sas_address(
                  fw_domain, &properties.remote.sas_address
               );

   // If the device already existed in the domain, it is a wide port SSP target,
   // we need to update its port width.
   if (fw_device != SCI_INVALID_HANDLE)
   {
      SMP_DISCOVER_RESPONSE_PROTOCOLS_T  dev_protocols;
      scic_remote_device_get_protocols(fw_device->core_object, &dev_protocols);

      if (dev_protocols.u.bits.attached_ssp_target)
      {
         //Get accurate port width from port's phy mask for a DA device.
         SCI_GET_BITS_SET_COUNT(properties.phy_mask, new_port_width);

         scif_sas_remote_device_update_port_width(fw_device, new_port_width);
      }
   }
}
#endif //#if !defined(DISABLE_WIDE_PORTED_TARGETS)


#ifdef SCI_LOGGING
/**
 * This method will turn on logging of domain state changes.
 *
 * @param[in] fw_domain The domain for which the state logging is to be turned
 *       on.
 */
void scif_sas_domain_initialize_state_logging(
   SCIF_SAS_DOMAIN_T *fw_domain
)
{
   sci_base_state_machine_logger_initialize(
      &fw_domain->parent.state_machine_logger,
      &fw_domain->parent.state_machine,
      &fw_domain->parent.parent,
      scif_cb_logger_log_states,
      "SCIF_SAS_DOMAIN_T", "base state machine",
      SCIF_LOG_OBJECT_DOMAIN
   );
}

/**
 * This method will turn off logging of domain state changes.
 *
 * @param[in] fw_domain The domain for which the state logging is to be turned
 *       off.
 */
void scif_sas_domain_deinitialize_state_logging(
   SCIF_SAS_DOMAIN_T *fw_domain
)
{
   sci_base_state_machine_logger_deinitialize(
      &fw_domain->parent.state_machine_logger,
      &fw_domain->parent.state_machine
   );
}
#endif
