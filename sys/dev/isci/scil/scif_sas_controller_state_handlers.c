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
 *        of the controller states defined by the SCI_BASE_CONTROLLER state
 *        machine.
 */

#include <dev/isci/scil/sci_util.h>
#include <dev/isci/scil/scic_controller.h>
#include <dev/isci/scil/scic_port.h>
#include <dev/isci/scil/scic_remote_device.h>
#include <dev/isci/scil/scic_io_request.h>

#include <dev/isci/scil/scif_sas_controller.h>
#include <dev/isci/scil/scif_sas_remote_device.h>
#include <dev/isci/scil/scif_sas_logger.h>
#include <dev/isci/scil/scif_sas_smp_remote_device.h>

//******************************************************************************
//* P R I V A T E   M E T H O D S
//******************************************************************************

/**
 * @brief This method simply executes the reset operation by entering
 *        the reset state and allowing the state to perform it's work.
 *
 * @param[in]  fw_controller This parameter specifies the SAS framework
 *             controller for execute the reset.
 *
 * @return Indicate the status of the reset operation.  Was it successful?
 * @retval SCI_SUCCESS This value is returned if it was successfully reset.
 */
static
SCI_STATUS scif_sas_controller_execute_reset(
   SCIF_SAS_CONTROLLER_T * fw_controller
)
{
   SCI_STATUS  status;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_controller),
      SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_CONTROLLER_RESET,
      "scif_sas_controller_execute_reset(0x%x) enter\n",
      fw_controller
   ));

   //clean the timer to avoid timer leak.
   scif_sas_controller_release_resource(fw_controller);

   sci_base_state_machine_change_state(
      &fw_controller->parent.state_machine,
      SCI_BASE_CONTROLLER_STATE_RESETTING
   );

   // Retrieve the status for the operations performed during the entrance
   // to the resetting state were executing successfully.
   status = fw_controller->operation_status;
   fw_controller->operation_status = SCI_SUCCESS;

   return status;
}

/**
 * @brief This method checks that the memory descriptor list is valid
 *        and hasn't been corrupted in some way by the user.
 *
 * @param[in] fw_controller This parameter specifies the framework
 *            controller object for which to validation the MDL.
 *
 * @return This method returns a value indicating if the operation succeeded.
 * @retval SCI_SUCCESS This value indicates that MDL is valid.
 * @retval SCI_FAILURE_UNSUPPORTED_INFORMATION_FIELD This value indicates
 *         that some portion of the memory descriptor list is invalid.
 */
static
SCI_STATUS scif_sas_controller_validate_mdl(
   SCIF_SAS_CONTROLLER_T * fw_controller
)
{
   BOOL is_mde_list_valid;

   // Currently there is only a single MDE in the list.
   is_mde_list_valid = sci_base_mde_is_valid(
                          &fw_controller->mdes[SCIF_SAS_MDE_INTERNAL_IO],
                          4,
                          fw_controller->internal_request_entries *
                             scif_sas_internal_request_get_object_size(),
                          SCI_MDE_ATTRIBUTE_PHYSICALLY_CONTIGUOUS
                       );

   if (is_mde_list_valid == FALSE)
      return SCI_FAILURE_UNSUPPORTED_INFORMATION_FIELD;

   return SCI_SUCCESS;
}


/**
 * @brief This method stops all the domains associated to this
 *           controller.
 *
 * @param[in] fw_controller This parameter specifies the framework
 *            controller object for whose remote devices are to be stopped.
 *
 * @return This method returns a value indicating if the operation succeeded.
 * @retval SCI_SUCCESS This value indicates that all the devices are stopped.
 * @retval SCI_FAILURE This value indicates certain failure during the process
 *            of stopping remote devices.
 */
static
SCI_STATUS scif_sas_controller_stop_domains(
   SCIF_SAS_CONTROLLER_T * fw_controller
)
{
   U8 index;
   SCI_STATUS status = SCI_SUCCESS;
   SCIF_SAS_DOMAIN_T * fw_domain;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_controller),
      SCIF_LOG_OBJECT_CONTROLLER,
      "scif_sas_controller_stop_domains(0x%x) enter\n",
      fw_controller
   ));

   for (index = 0; index < SCI_MAX_DOMAINS && status == SCI_SUCCESS; index++)
   {
      fw_domain = &fw_controller->domains[index];

      //Change this domain to STOPPING state. All the remote devices will be
      //stopped subsquentially.
      if (fw_domain->parent.state_machine.current_state_id ==
             SCI_BASE_DOMAIN_STATE_READY
          || fw_domain->parent.state_machine.current_state_id ==
             SCI_BASE_DOMAIN_STATE_DISCOVERING)
      {
         sci_base_state_machine_change_state(
            &fw_domain->parent.state_machine, SCI_BASE_DOMAIN_STATE_STOPPING
         );
      }
   }

   return status;
}


/**
 * @brief This method continue to stop the controller after clear affiliation
 *        is done.
 *
 * @param[in] fw_controller This parameter specifies the framework
 *            controller object to be stopped.
 *
 * @return This method returns a value indicating if the operation succeeded.
 * @retval SCI_SUCCESS This value indicates the controller_stop succeeds.
 * @retval SCI_FAILURE This value indicates certain failure during the process
 *            of stopping controller.
 */
SCI_STATUS scif_sas_controller_continue_to_stop(
   SCIF_SAS_CONTROLLER_T * fw_controller
)
{
   SCI_STATUS status;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_controller),
      SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_SHUTDOWN,
      "scif_sas_controller_continue_to_stop (0x%x).\n",
      fw_controller
   ));

   //stop all the domains and their remote devices.
   status = scif_sas_controller_stop_domains(fw_controller);

   if (status == SCI_SUCCESS)
   {
      // Attempt to stop the core controller.
      status = scic_controller_stop(fw_controller->core_object, 0);

      if (status != SCI_SUCCESS)
      {
         SCIF_LOG_ERROR((
            sci_base_object_get_logger(fw_controller),
            SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_SHUTDOWN,
            "Controller:0x%x Status:0x%x unable to stop controller.\n",
            fw_controller, status
         ));

         sci_base_state_machine_change_state(
            &fw_controller->parent.state_machine,
            SCI_BASE_CONTROLLER_STATE_FAILED
         );
      }
   }
   else
   {
      SCIF_LOG_ERROR((
         sci_base_object_get_logger(fw_controller),
         SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_SHUTDOWN,
         "Controller:0x%x Status:0x%x unable to stop domains.\n",
         fw_controller, status
      ));

      sci_base_state_machine_change_state(
         &fw_controller->parent.state_machine,
         SCI_BASE_CONTROLLER_STATE_FAILED
      );
   }

   return status;
}


//******************************************************************************
//* R E S E T   H A N D L E R S
//******************************************************************************

/**
 * @brief This method provides RESET state specific handling for
 *        when a user attempts to initialize a controller.  This is a legal
 *        state in which to attempt an initialize call.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             on which the user is attempting to perform an initialize
 *             operation.
 *
 * @return This method returns an indication of whether the initialize
 *         operation succeeded.
 * @retval SCI_SUCCESS This value when the initialization completes
 *         successfully.
 */
static
SCI_STATUS scif_sas_controller_reset_initialize_handler(
   SCI_BASE_CONTROLLER_T    * controller
)
{
   SCIF_SAS_CONTROLLER_T * fw_controller = (SCIF_SAS_CONTROLLER_T *)controller;
   SCI_STATUS              status;
   U32                     index;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_controller),
      SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_INITIALIZATION,
      "scif_sas_controller_reset_initialize_handler(0x%x) enter\n",
      controller
   ));

   sci_base_state_machine_change_state(
      &fw_controller->parent.state_machine,
      SCI_BASE_CONTROLLER_STATE_INITIALIZING
   );

   scif_sas_controller_build_mdl(fw_controller);

   // Perform any domain object initialization that is necessary.
   for (index = 0; index < SCI_MAX_DOMAINS; index++)
      scif_sas_domain_initialize(&fw_controller->domains[index]);

   scif_cb_lock_associate(fw_controller, &fw_controller->hprq.lock);

   // Attempt to initialize the core controller.
   status = scic_controller_initialize(fw_controller->core_object);
   if (status == SCI_SUCCESS)
   {
      sci_base_state_machine_change_state(
         &fw_controller->parent.state_machine,
         SCI_BASE_CONTROLLER_STATE_INITIALIZED
      );
   }

   if (status != SCI_SUCCESS)
   {
      // Initialization failed, Release resources and do not change state
      scif_sas_controller_release_resource(fw_controller);

      SCIF_LOG_ERROR((
         sci_base_object_get_logger(fw_controller),
         SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_INITIALIZATION,
         "Controller:0x%x Status:0x%x unable to successfully initialize.\n",
         fw_controller, status
      ));
   }

   return status;
}

//******************************************************************************
//* I N I T I A L I Z E D   H A N D L E R S
//******************************************************************************

/**
 * @brief This method provides INITIALIZED state specific handling for
 *        when a user attempts to start a controller.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             on which the user is attempting to perform a start
 *             operation.
 * @param[in]  timeout This parameter specifies the timeout value (in
 *             milliseconds) to be utilized for this operation.
 *
 * @return This method returns an indication of whether the start operation
 *         succeeded.
 * @retval SCI_SUCCESS This value is returned when the start operation
 *         begins successfully.
 */
static
SCI_STATUS scif_sas_controller_initialized_start_handler(
   SCI_BASE_CONTROLLER_T * controller,
   U32                     timeout
)
{
   SCI_STATUS              status        = SCI_SUCCESS;
   SCIF_SAS_CONTROLLER_T * fw_controller = (SCIF_SAS_CONTROLLER_T *)controller;
   U16                     index         = 0;

   SCI_PHYSICAL_MEMORY_DESCRIPTOR_T internal_reqeust_mde =
      fw_controller->mdes[SCIF_SAS_MDE_INTERNAL_IO];

   void * internal_request_virtual_address =  internal_reqeust_mde.virtual_address;
   POINTER_UINT address = (POINTER_UINT)internal_request_virtual_address;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_controller),
      SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_INITIALIZATION,
      "scif_sas_controller_initialized_start_handler(0x%x, 0x%x) enter\n",
      controller, timeout
   ));

   sci_base_state_machine_change_state(
      &fw_controller->parent.state_machine,
      SCI_BASE_CONTROLLER_STATE_STARTING
   );

   status = scif_sas_controller_validate_mdl(fw_controller);

   // initialization work for internal request path. It must be done before
   // starting domain.
   if (status == SCI_SUCCESS)
   {
      // fill in the sci_pool for internal requests.
      sci_pool_initialize(fw_controller->internal_request_memory_pool);

      for (index = 0; index < fw_controller->internal_request_entries; index++)
      {
         sci_pool_put(fw_controller->internal_request_memory_pool, address);

         address += scif_sas_internal_request_get_object_size();
      }

      // Using DPC for starting internal IOs, if yes, we need to intialize
      // DPC here.
      scif_cb_start_internal_io_task_create(fw_controller);
   }

   if (status == SCI_SUCCESS)
   {
      // Kick-start the domain state machines and, by association, the
      // core port's.

      // This will ensure we get valid port objects supplied with link up
      // messages.
      for (index = 0;
           (index < SCI_MAX_DOMAINS) && (status == SCI_SUCCESS);
           index++)
      {
         sci_base_state_machine_change_state(
            &fw_controller->domains[index].parent.state_machine,
            SCI_BASE_DOMAIN_STATE_STARTING
         );
         status = fw_controller->domains[index].operation.status;
      }
   }

   // Validate that all the domain state machines began successfully.
   if (status != SCI_SUCCESS)
   {
      SCIF_LOG_ERROR((
         sci_base_object_get_logger(fw_controller),
         SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_INITIALIZATION,
         "Controller:0x%x Domain:0x%x Status:0x%x unable to start\n",
         fw_controller, index, status
      ));

      return status;
   }

   // Attempt to start the core controller.
   status = scic_controller_start(fw_controller->core_object, timeout);
   if (status != SCI_SUCCESS)
   {
      SCIF_LOG_ERROR((
         sci_base_object_get_logger(fw_controller),
         SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_INITIALIZATION,
         "Controller:0x%x Status:0x%x unable to start controller.\n",
         fw_controller, status
      ));

      sci_base_state_machine_change_state(
         &fw_controller->parent.state_machine,
         SCI_BASE_CONTROLLER_STATE_FAILED
      );
   }

   return status;
}

//******************************************************************************
//* R E A D Y   H A N D L E R S
//******************************************************************************

/**
 * @brief This method provides READY state specific handling for
 *        when a user attempts to stop a controller.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             on which the user is attempting to perform a stop
 *             operation.
 * @param[in]  timeout This parameter specifies the timeout value (in
 *             milliseconds) to be utilized for this operation.
 *
 * @return This method returns an indication of whether the stop operation
 *         succeeded.
 * @retval SCI_SUCCESS This value is returned when the stop operation
 *         begins successfully.
 */
static
SCI_STATUS scif_sas_controller_ready_stop_handler(
   SCI_BASE_CONTROLLER_T * controller,
   U32                     timeout
)
{
   SCIF_SAS_CONTROLLER_T * fw_controller = (SCIF_SAS_CONTROLLER_T *)controller;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_controller),
      SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_INITIALIZATION,
      "scif_sas_controller_ready_stop_handler(0x%x, 0x%x) enter\n",
      controller, timeout
   ));

   sci_base_state_machine_change_state(
      &fw_controller->parent.state_machine,
      SCI_BASE_CONTROLLER_STATE_STOPPING
   );

   if (fw_controller->user_parameters.sas.clear_affiliation_during_controller_stop)
   {
      fw_controller->current_domain_to_clear_affiliation = 0;

      //clear affiliation first. After the last domain finishes clearing
      //affiliation, it will call back to controller to continue to stop.
      scif_sas_controller_clear_affiliation(fw_controller);
   }
   else
      scif_sas_controller_continue_to_stop(fw_controller);

   //Must return SUCCESS at this point.
   return SCI_SUCCESS;
}

/**
 * @brief This method provides READY state specific handling for
 *        when a user attempts to reset a controller.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             on which the user is attempting to perform a reset
 *             operation.
 *
 * @return This method returns an indication of whether the reset operation
 *         succeeded.
 * @retval SCI_SUCCESS This value is returned when the reset operation
 *         completes successfully.
 */
static
SCI_STATUS scif_sas_controller_ready_reset_handler(
   SCI_BASE_CONTROLLER_T    * controller
)
{
   return scif_sas_controller_execute_reset((SCIF_SAS_CONTROLLER_T*)controller);
}

/**
 * @brief This method provides READY state specific handling for
 *        when a user attempts to start an IO request.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             on which the user is attempting to perform a start IO
 *             operation.
 * @param[in]  remote_device This parameter specifies the remote deivce
 *             object on which the user is attempting to perform a start IO
 *             operation.
 * @param[in]  io_request This parameter specifies the IO request to be
 *             started.
 * @param[in]  io_tag This parameter specifies the optional allocated
 *             IO tag.  Please reference scif_controller_start_io() for
 *             more information.
 *
 * @return This method returns an indication of whether the start IO
 *         operation succeeded.
 * @retval SCI_SUCCESS This value is returned when the start IO operation
 *         begins successfully.
 */
static
SCI_STATUS scif_sas_controller_ready_start_io_handler(
   SCI_BASE_CONTROLLER_T    * controller,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request,
   U16                        io_tag
)
{
   SCI_STATUS                status;
   SCIF_SAS_IO_REQUEST_T    *fw_io         = (SCIF_SAS_IO_REQUEST_T*)io_request;
   SCIF_SAS_CONTROLLER_T    *fw_controller = (SCIF_SAS_CONTROLLER_T*)controller;
   SCIF_SAS_REMOTE_DEVICE_T *fw_device     = (SCIF_SAS_REMOTE_DEVICE_T*)
                                             remote_device;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_controller),
      SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_sas_controller_ready_start_io_handler(0x%x, 0x%x, 0x%x, 0x%x) enter\n",
      controller, remote_device, io_request, io_tag
   ));

   status = fw_device->domain->state_handlers->start_io_handler(
               &fw_device->domain->parent, remote_device, io_request
            );

   // Check to see that the other objects in the framework allowed
   // this IO to be started.
   if (status == SCI_SUCCESS)
   {
      // Ask the core to start processing for this IO request.
      status = (SCI_STATUS)scic_controller_start_io(
                  fw_controller->core_object,
                  fw_device->core_object,
                  fw_io->parent.core_object,
                  io_tag
               );

      if (status == SCI_SUCCESS)
      {
         // We were able to start the core request. As a result,
         // commit to starting the request for the framework by changing
         // the state of the IO request.
         sci_base_state_machine_change_state(
            &io_request->state_machine, SCI_BASE_REQUEST_STATE_STARTED
         );
      }
      else
      {
         // We were unable to start the core IO request. As a result,
         // back out the start operation for the framework.  It's easier to
         // back out the framework start operation then to backout the core
         // start IO operation.
         fw_device->domain->state_handlers->complete_io_handler(
            &fw_device->domain->parent, remote_device, io_request
         );

         // Invoke the IO completion handler.  For most IOs, this does nothing
         // since we are still in the constructed state.  For NCQ, this will
         // the return of the NCQ tag back to the remote device free pool.
         fw_io->parent.state_handlers->complete_handler(io_request);

         SCIF_LOG_WARNING((
            sci_base_object_get_logger(fw_controller),
            SCIF_LOG_OBJECT_CONTROLLER,
            "Controller:0x%x IORequest:0x%x Status:0x%x core IO start failed\n",
            fw_controller, fw_io, status
         ));
      }
   }
   else
   {
      SCIF_LOG_WARNING((
         sci_base_object_get_logger(fw_controller),
         SCIF_LOG_OBJECT_CONTROLLER,
         "Controller:0x%x IORequest:0x%x Status:0x%x IO start failed\n",
         fw_controller, fw_io, status
      ));
   }

   return status;
}

/**
 * @brief This method provides READY state specific handling for
 *        when a user attempts to complete an IO request.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             on which the user is attempting to perform a complete IO
 *             operation.
 * @param[in]  remote_device This parameter specifies the remote deivce
 *             object on which the user is attempting to perform a start IO
 *             operation.
 * @param[in]  io_request This parameter specifies the IO request to be
 *             started.
 *
 * @return This method returns an indication of whether the complete IO
 *         operation succeeded.
 * @retval SCI_SUCCESS This value is returned when the complete IO operation
 *         begins successfully.
 */
static
SCI_STATUS scif_sas_controller_ready_complete_io_handler(
   SCI_BASE_CONTROLLER_T    * controller,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request
)
{
   SCIF_SAS_CONTROLLER_T    * fw_controller = (SCIF_SAS_CONTROLLER_T*)
                                              controller;
   SCIF_SAS_REMOTE_DEVICE_T * fw_device     = (SCIF_SAS_REMOTE_DEVICE_T*)
                                              remote_device;
   SCIF_SAS_IO_REQUEST_T    * fw_io         = (SCIF_SAS_IO_REQUEST_T*)
                                              io_request;
   SCI_STATUS                 status;
   SCI_STATUS                 core_status;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_controller),
      SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_sas_controller_ready_complete_io_handler(0x%x, 0x%x, 0x%x) enter\n",
      controller, remote_device, io_request
   ));

   fw_io->parent.state_handlers->destruct_handler(&fw_io->parent.parent);
   status = fw_device->domain->state_handlers->complete_io_handler(
               &fw_device->domain->parent, remote_device, io_request
            );

   // Ask the core to finish processing for this IO request.
   core_status = scic_controller_complete_io(
                    fw_controller->core_object,
                    fw_device->core_object,
                    fw_io->parent.core_object
                 );

   if (status == SCI_SUCCESS)
      status = core_status;

   if (status != SCI_SUCCESS)
   {
      SCIF_LOG_WARNING((
         sci_base_object_get_logger(fw_controller),
         SCIF_LOG_OBJECT_CONTROLLER,
         "Controller:0x%x IORequest:0x%x Status:0x%x CoreStatus:0x%x "
         "failure to complete IO\n",
         fw_controller, fw_io, status, core_status
      ));
   }

   return status;
}


/**
 * @brief This method provides READY state specific handling for
 *        when a user attempts to complete a high priority IO request.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             on which the user is attempting to perform a complete IO
 *             operation.
 * @param[in]  remote_device This parameter specifies the remote deivce
 *             object on which the user is attempting to perform a start IO
 *             operation.
 * @param[in]  io_request This parameter specifies the IO request to be
 *             started.
 *
 * @return This method returns an indication of whether the complete IO
 *         operation succeeded.
 * @retval SCI_SUCCESS This value is returned when the complete IO operation
 *         begins successfully.
 */
static
SCI_STATUS scif_sas_controller_ready_complete_high_priority_io_handler(
   SCI_BASE_CONTROLLER_T    * controller,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request
)
{
   SCIF_SAS_CONTROLLER_T    * fw_controller = (SCIF_SAS_CONTROLLER_T*)
                                              controller;
   SCIF_SAS_REMOTE_DEVICE_T * fw_device     = (SCIF_SAS_REMOTE_DEVICE_T*)
                                              remote_device;
   SCIF_SAS_IO_REQUEST_T    * fw_io         = (SCIF_SAS_IO_REQUEST_T*)
                                              io_request;
   SCI_IO_STATUS core_completion_status =
                    scic_request_get_sci_status(fw_io->parent.core_object);

   U8 response_data[SCIF_SAS_RESPONSE_DATA_LENGTH];

   SCI_STATUS                 status;
   SCI_STATUS                 core_status;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_controller),
      SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_sas_controller_ready_complete_high_priority_io_handler(0x%x, 0x%x, 0x%x) enter\n",
      controller, remote_device, io_request
   ));

   // In high priority path, we ask the core to finish IO request before framework.

   // retrieve and save io response from core now.
   memcpy(response_data,
          scic_io_request_get_response_iu_address(fw_io->parent.core_object),
          SCIF_SAS_RESPONSE_DATA_LENGTH
         );

   core_status = scic_controller_complete_io(
                    fw_controller->core_object,
                    fw_device->core_object,
                    fw_io->parent.core_object
                 );

   fw_io->parent.state_handlers->destruct_handler(&fw_io->parent.parent);
   status = fw_device->domain->state_handlers->complete_high_priority_io_handler(
               &fw_device->domain->parent,
               remote_device,
               io_request,
               (void *)response_data,
               core_completion_status
            );

   if (status == SCI_SUCCESS)
      status = core_status;

   if (status == SCI_SUCCESS)
   {
       //issue DPC to start next internal io in high prioriy queue.
      if( !sci_pool_empty(fw_controller->hprq.pool) )
         scif_cb_start_internal_io_task_schedule(
            fw_controller,
            scif_sas_controller_start_high_priority_io,
            fw_controller
         );
   }
   else
   {
      SCIF_LOG_WARNING((
         sci_base_object_get_logger(fw_controller),
         SCIF_LOG_OBJECT_CONTROLLER,
         "Controller:0x%x IORequest:0x%x Status:0x%x CoreStatus:0x%x "
         "failure to complete IO\n",
         fw_controller, fw_io, status, core_status
      ));
   }

   return status;
}

/**
 * @brief This method provides READY state specific handling for
 *        when a user attempts to continue an IO request.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             on which the user is attempting to perform a continue IO
 *             operation.
 * @param[in]  remote_device This parameter specifies the remote deivce
 *             object on which the user is attempting to perform a start IO
 *             operation.
 * @param[in]  io_request This parameter specifies the IO request to be
 *             started.
 *
 * @return This method returns an indication of whether the continue IO
 *         operation succeeded.
 * @retval SCI_SUCCESS This value is returned when the continue IO operation
 *         begins successfully.
 */
static
SCI_STATUS scif_sas_controller_ready_continue_io_handler(
   SCI_BASE_CONTROLLER_T    * controller,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request
)
{
   SCIF_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_sas_controller_ready_continue_io_handler(0x%x, 0x%x, 0x%x) enter\n",
      controller, remote_device, io_request
   ));

   /// @todo Function unimplemented.  fix return code handling.
   return SCI_FAILURE;
}

/**
 * @brief This method provides READY state specific handling for
 *        when a user attempts to start a task request.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             on which the user is attempting to perform a start task
 *             operation.
 * @param[in]  remote_device This parameter specifies the remote deivce
 *             object on which the user is attempting to perform a start
 *             task operation.
 * @param[in]  task_request This parameter specifies the task management
 *             request to be started.
 * @param[in]  io_tag This parameter specifies the optional allocated
 *             IO tag.  Please reference scif_controller_start_task() for
 *             more information.
 *
 * @return This method returns an indication of whether the start task
 *         operation succeeded.
 * @retval SCI_SUCCESS This value is returned when the start task operation
 *         begins successfully.
 */
static
SCI_STATUS scif_sas_controller_ready_start_task_handler(
   SCI_BASE_CONTROLLER_T    * controller,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * task_request,
   U16                        io_tag
)
{
   SCIF_SAS_CONTROLLER_T    * fw_controller = (SCIF_SAS_CONTROLLER_T*)
                                              controller;
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T*)
                                          remote_device;
   SCIF_SAS_TASK_REQUEST_T  * fw_task = (SCIF_SAS_TASK_REQUEST_T*)task_request;
   SCI_STATUS                 status;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_controller),
      SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_TASK_MANAGEMENT,
      "scif_sas_controller_ready_start_task_handler(0x%x, 0x%x, 0x%x, 0x%x) enter\n",
      controller, remote_device, task_request, io_tag
   ));

   status = fw_device->domain->state_handlers->start_task_handler(
               &fw_device->domain->parent, remote_device, task_request
            );

   if (status == SCI_SUCCESS)
   {
      if (scif_sas_task_request_get_function(fw_task)
             == SCI_SAS_HARD_RESET)
      {
         // Go off to special target reset path. Don't start task to core.
         scif_sas_remote_device_target_reset(
            fw_device,
            (SCIF_SAS_REQUEST_T *)fw_task
         );

         return SCI_SUCCESS;
      }

      // Ask the core to start processing for this task request.
      status = (SCI_STATUS)scic_controller_start_task(
                  fw_controller->core_object,
                  fw_device->core_object,
                  fw_task->parent.core_object,
                  io_tag
               );

      if (status == SCI_SUCCESS)
      {
         // We were able to start the core request. As a result,
         // commit to starting the request for the framework by changing
         // the state of the task request.
         fw_task->parent.state_handlers->start_handler(&fw_task->parent.parent);
      }
      else
      {
         // We were unable to start the core task request. As a result,
         // back out the start operation for the framework.  It's easier to
         // back out the framework start operation then to backout the core
         // start task operation.
         fw_device->domain->state_handlers->complete_task_handler(
            &fw_device->domain->parent, remote_device, task_request
         );

         if (status == SCI_SUCCESS)
         {
            SCIF_LOG_WARNING((
               sci_base_object_get_logger(fw_controller),
               SCIF_LOG_OBJECT_CONTROLLER,
               "Controller:0x%x TaskRequest:0x%x Status:0x%x core start failed\n",
               fw_controller, fw_task, status
            ));
         }
      }
   }
   else
   {
      SCIF_LOG_WARNING((
         sci_base_object_get_logger(fw_controller),
         SCIF_LOG_OBJECT_CONTROLLER,
         "Controller:0x%x TaskRequest:0x%x Status:0x%x Task start failed\n",
         fw_controller, fw_task, status
      ));
   }

   return status;
}

/**
 * @brief This method provides READY state specific handling for
 *        when a user attempts to complete a task request.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             on which the user is attempting to perform a complete task
 *             operation.
 * @param[in]  remote_device This parameter specifies the remote deivce
 *             object on which the user is attempting to perform a start
 *             task operation.
 * @param[in]  task_request This parameter specifies the task management
 *             request to be started.
 *
 * @return This method returns an indication of whether the complete task
 *         operation succeeded.
 * @retval SCI_SUCCESS This value is returned when the complete task operation
 *         begins successfully.
 */
static
SCI_STATUS scif_sas_controller_ready_complete_task_handler(
   SCI_BASE_CONTROLLER_T    * controller,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * task_request
)
{
   SCIF_SAS_CONTROLLER_T    *fw_controller = (SCIF_SAS_CONTROLLER_T*)controller;
   SCIF_SAS_REMOTE_DEVICE_T *fw_device = (SCIF_SAS_REMOTE_DEVICE_T*)remote_device;
   SCIF_SAS_TASK_REQUEST_T  *fw_task = (SCIF_SAS_TASK_REQUEST_T*)task_request;
   SCI_STATUS                status;
   SCI_STATUS                core_status;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_controller),
      SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_TASK_MANAGEMENT,
      "scif_sas_controller_ready_complete_task_handler(0x%x, 0x%x, 0x%x) enter\n",
      controller, remote_device, task_request
   ));

   status = fw_device->domain->state_handlers->complete_task_handler(
               &fw_device->domain->parent, remote_device, task_request
            );

   if (scif_sas_task_request_get_function(fw_task)
          == SCI_SAS_HARD_RESET)
   {
      //No more things to do in the core, since this task is for Target Reset.
      return status;
   }

   fw_task->parent.state_handlers->destruct_handler(&fw_task->parent.parent);

   // Ask the core to finish processing for this task request.
   core_status = scic_controller_complete_task(
                    fw_controller->core_object,
                    fw_device->core_object,
                    fw_task->parent.core_object
                 );

   if (status == SCI_SUCCESS)
      status = core_status;

   if (status != SCI_SUCCESS)
   {
      SCIF_LOG_WARNING((
         sci_base_object_get_logger(fw_controller),
         SCIF_LOG_OBJECT_CONTROLLER,
         "Controller:0x%x TaskRequest:0x%x Status:0x%x CoreStatus:0x%x "
         "failed to complete\n",
         fw_controller, fw_task, status, core_status
      ));
   }

   return status;
}



/**
 * @brief This method provides common handling for several states
 *        when a user attempts to start an internal request.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             on which the user is attempting to perform a start IO
 *             operation.
 * @param[in]  remote_device This parameter specifies the remote deivce
 *             object on which the user is attempting to perform a start IO
 *             operation.
 * @param[in]  io_request This parameter specifies the IO request to be
 *             started.
 * @param[in]  io_tag This parameter specifies the optional allocated
 *             IO tag.  Please reference scif_controller_start_io() for
 *             more information.
 *
 * @return This method returns an indication of whether the start IO
 *         operation succeeded.
 * @retval SCI_SUCCESS This value is returned when the start IO operation
 *         begins successfully.
 */
static
SCI_STATUS scif_sas_controller_common_start_high_priority_io_handler(
   SCI_BASE_CONTROLLER_T    * controller,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request,
   U16                        io_tag
)
{
   SCI_STATUS                status;
   SCIF_SAS_IO_REQUEST_T    *fw_io         = (SCIF_SAS_IO_REQUEST_T*)io_request;
   SCIF_SAS_CONTROLLER_T    *fw_controller = (SCIF_SAS_CONTROLLER_T*)controller;
   SCIF_SAS_REMOTE_DEVICE_T *fw_device     = (SCIF_SAS_REMOTE_DEVICE_T*)
                                             remote_device;

   status = fw_device->domain->state_handlers->start_high_priority_io_handler(
               &fw_device->domain->parent, remote_device, io_request
            );

   // Check to see that the other objects in the framework allowed
   // this IO to be started.
   if (status == SCI_SUCCESS)
   {
      // Ask the core to start processing for this IO request.
      status = (SCI_STATUS)scic_controller_start_io(
                  fw_controller->core_object,
                  fw_device->core_object,
                  fw_io->parent.core_object,
                  io_tag
               );

      if (status == SCI_SUCCESS)
      {
         // We were able to start the core request. As a result,
         // commit to starting the request for the framework by changing
         // the state of the IO request.
         sci_base_state_machine_change_state(
            &io_request->state_machine, SCI_BASE_REQUEST_STATE_STARTED
         );
      }
      else
      {
         // We were unable to start the core IO request. As a result,
         // back out the start operation for the framework.  It's easier to
         // back out the framework start operation then to backout the core
         // start IO operation.
         fw_device->domain->state_handlers->complete_io_handler(
            &fw_device->domain->parent, remote_device, io_request
         );

         // Invoke the IO completion handler.  For most IOs, this does nothing
         // since we are still in the constructed state.  For NCQ, this will
         // the return of the NCQ tag back to the remote device free pool.
         fw_io->parent.state_handlers->complete_handler(io_request);

         SCIF_LOG_WARNING((
            sci_base_object_get_logger(fw_controller),
            SCIF_LOG_OBJECT_CONTROLLER,
            "Controller:0x%x IORequest:0x%x Status:0x%x core IO start failed\n",
            fw_controller, fw_io, status
         ));
      }
   }
   else
   {
      SCIF_LOG_WARNING((
         sci_base_object_get_logger(fw_controller),
         SCIF_LOG_OBJECT_CONTROLLER,
         "Controller:0x%x IORequest:0x%x Status:0x%x IO start failed\n",
         fw_controller, fw_io, status
      ));

      // Invoke the IO completion handler.  For most IOs, this does nothing
      // since we are still in the constructed state.  For NCQ, this will
      // the return of the NCQ tag back to the remote device free pool.
      fw_io->parent.state_handlers->complete_handler(io_request);

   }

   if (fw_io->parent.is_internal && status != SCI_SUCCESS )
   {
      SCIC_TRANSPORT_PROTOCOL protocol =
         scic_io_request_get_protocol(fw_io->parent.core_object);

      U8 retry_count = fw_io->retry_count;

      scif_sas_internal_io_request_destruct(
         fw_device->domain->controller,
         (SCIF_SAS_INTERNAL_IO_REQUEST_T *)fw_io
      );

      if ( protocol == SCIC_SMP_PROTOCOL )
      {
         if (fw_device->protocol_device.smp_device.smp_activity_timer != NULL)
         {
            //destroy the smp_activity_timer
            scif_cb_timer_destroy (
               fw_controller,
               fw_device->protocol_device.smp_device.smp_activity_timer
            );

            fw_device->protocol_device.smp_device.smp_activity_timer = NULL;
         }

         //we should retry for finite times
         if ( retry_count < SCIF_SAS_IO_RETRY_LIMIT)
         {
         //An internal smp request failed being started, most likely due to remote device
         //is not in ready state, for example, UPDATING_PORT_WIDTH state. In this case,
         //we should retry the IO.
         scif_sas_smp_remote_device_retry_internal_io(
            (SCIF_SAS_REMOTE_DEVICE_T *)remote_device,
            retry_count,
            SMP_REQUEST_RETRY_WAIT_DURATION
         );
      }
   }
   }

   return status;
}


/**
 * @brief This method provides READY state specific handling for
 *        when a user attempts to start an internal request. If the high
 *        priority IO is also internal, this method will schedule its timer.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             on which the user is attempting to perform a start IO
 *             operation.
 * @param[in]  remote_device This parameter specifies the remote deivce
 *             object on which the user is attempting to perform a start IO
 *             operation.
 * @param[in]  io_request This parameter specifies the IO request to be
 *             started.
 * @param[in]  io_tag This parameter specifies the optional allocated
 *             IO tag.  Please reference scif_controller_start_io() for
 *             more information.
 *
 * @return This method returns an indication of whether the start IO
 *         operation succeeded.
 * @retval SCI_SUCCESS This value is returned when the start IO operation
 *         begins successfully.
 */
static
SCI_STATUS scif_sas_controller_ready_start_high_priority_io_handler(
   SCI_BASE_CONTROLLER_T    * controller,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request,
   U16                        io_tag
)
{
   SCI_STATUS status;
   SCIF_SAS_IO_REQUEST_T * fw_io = (SCIF_SAS_IO_REQUEST_T *)io_request;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_sas_controller_ready_start_high_priority_io_handler(0x%x, 0x%x, 0x%x, 0x%x) enter\n",
      controller, remote_device, io_request, io_tag
   ));

   status = scif_sas_controller_common_start_high_priority_io_handler(
               controller, remote_device, io_request, io_tag);

   if (status == SCI_SUCCESS)
   {
      //External io could also be put in high priority queue. i.e. the
      //smp request for EA Target Reset.
      if (fw_io->parent.is_internal)
      {
         SCIF_SAS_INTERNAL_IO_REQUEST_T * fw_internal_io =
            (SCIF_SAS_INTERNAL_IO_REQUEST_T *)fw_io;

         //start the timer for internal io
         scif_cb_timer_start(
            (SCI_CONTROLLER_HANDLE_T)controller,
             fw_internal_io->internal_io_timer,
             SCIF_SAS_INTERNAL_REQUEST_TIMEOUT
         );
      }
   }
   else
   {
      //If failed to start, most likely the device or domain is not in
      //correct state, and the IO has been cleaned up in controller's start
      //high priority IO handler. We should just continue to start the next
      //IO in the HP queue.

      SCIF_LOG_TRACE((
         sci_base_object_get_logger(controller),
         SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_IO_REQUEST,
         "scif_controller_start_high_priority_io(0x%x, 0x%x), starting io failed\n",
         controller, fw_io
      ));
   }

   return status;
}


//******************************************************************************
//* S T O P P I N G   H A N D L E R S
//******************************************************************************
/**
 * @brief This method provides STOPPING state specific handling for
 *        when a user attempts to start an internal request. Note that we don't
 *        start the timer for internal IO during controller stopping state.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             on which the user is attempting to perform a start IO
 *             operation.
 * @param[in]  remote_device This parameter specifies the remote deivce
 *             object on which the user is attempting to perform a start IO
 *             operation.
 * @param[in]  io_request This parameter specifies the IO request to be
 *             started.
 * @param[in]  io_tag This parameter specifies the optional allocated
 *             IO tag.  Please reference scif_controller_start_io() for
 *             more information.
 *
 * @return This method returns an indication of whether the start IO
 *         operation succeeded.
 * @retval SCI_SUCCESS This value is returned when the start IO operation
 *         begins successfully.
 */
static
SCI_STATUS scif_sas_controller_stopping_start_high_priority_io_handler(
   SCI_BASE_CONTROLLER_T    * controller,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request,
   U16                        io_tag
)
{
   SCIF_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_sas_controller_stopping_start_high_priority_io_handler(0x%x, 0x%x, 0x%x, 0x%x) enter\n",
      controller, remote_device, io_request, io_tag
   ));

   return scif_sas_controller_common_start_high_priority_io_handler(
             controller, remote_device, io_request, io_tag);
}


//******************************************************************************
//* S T O P P E D   H A N D L E R S
//******************************************************************************

/**
 * @brief This method provides STOPPED state specific handling for
 *        when a user attempts to reset a controller.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             on which the user is attempting to perform a reset
 *             operation.
 *
 * @return This method returns an indication of whether the reset operation
 *         succeeded.
 * @retval SCI_SUCCESS This value is returned when the reset operation
 *         completes successfully.
 */
static
SCI_STATUS scif_sas_controller_stopped_reset_handler(
   SCI_BASE_CONTROLLER_T    * controller
)
{
   return scif_sas_controller_execute_reset((SCIF_SAS_CONTROLLER_T*)controller);
}


//******************************************************************************
//* F A I L E D   H A N D L E R S
//******************************************************************************

/**
 * @brief This method provides FAILED state specific handling for
 *        when a user attempts to reset a controller.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             on which the user is attempting to perform a reset
 *             operation.
 *
 * @return This method returns an indication of whether the reset operation
 *         succeeded.
 * @retval SCI_SUCCESS This value is returned when the reset operation
 *         completes successfully.
 */
static
SCI_STATUS scif_sas_controller_failed_reset_handler(
   SCI_BASE_CONTROLLER_T * controller
)
{
   return scif_sas_controller_execute_reset((SCIF_SAS_CONTROLLER_T*)controller);
}

//******************************************************************************
//* D E F A U L T   H A N D L E R S
//******************************************************************************

/**
 * @brief This method provides default handling (i.e. returns an error)
 *        when a user attempts to start a controller and a start operation
 *        is not allowed.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             on which the user is attempting to perform a start operation.
 * @param[in]  timeout This parameter specifies the timeout value (in
 *             milliseconds) to be utilized for this operation.
 *
 * @return This method returns an indication that start operations are not
 *         allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
static
SCI_STATUS scif_sas_controller_default_start_handler(
   SCI_BASE_CONTROLLER_T * controller,
   U32                     timeout
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger((SCIF_SAS_CONTROLLER_T *)controller),
      SCIF_LOG_OBJECT_CONTROLLER,
      "Controller:0x%x State:0x%x invalid state to start controller.\n",
      controller,
      sci_base_state_machine_get_state(
         &((SCIF_SAS_CONTROLLER_T *)controller)->parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * @brief This method provides default handling (i.e. returns an error)
 *        when a user attempts to stop a controller and a stop operation
 *        is not allowed.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             on which the user is attempting to perform a stop operation.
 * @param[in]  timeout This parameter specifies the timeout value (in
 *             milliseconds) to be utilized for this operation.
 *
 * @return This method returns an indication that stop operations are not
 *         allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
static
SCI_STATUS scif_sas_controller_default_stop_handler(
   SCI_BASE_CONTROLLER_T * controller,
   U32                     timeout
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger((SCIF_SAS_CONTROLLER_T *)controller),
      SCIF_LOG_OBJECT_CONTROLLER,
      "Controller:0x%x State:0x%x invalid state to stop controller.\n",
      controller,
      sci_base_state_machine_get_state(
         &((SCIF_SAS_CONTROLLER_T *)controller)->parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * @brief This method provides default handling (i.e. returns an error)
 *        when a user attempts to reset a controller and a reset operation
 *        is not allowed.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             on which the user is attempting to perform a reset operation.
 *
 * @return This method returns an indication that reset operations are not
 *         allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
static
SCI_STATUS scif_sas_controller_default_reset_handler(
   SCI_BASE_CONTROLLER_T * controller
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger((SCIF_SAS_CONTROLLER_T *)controller),
      SCIF_LOG_OBJECT_CONTROLLER,
      "Controller:0x%x State:0x%x invalid state to reset controller.\n",
      controller,
      sci_base_state_machine_get_state(
         &((SCIF_SAS_CONTROLLER_T *)controller)->parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * @brief This method provides default handling (i.e. returns an error)
 *        when a user attempts to initialize a controller and an initialize
 *        operation is not allowed.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             on which the user is attempting to perform an initialize
 *             operation.
 *
 * @return This method returns an indication that initialize operations
 *         are not allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
static
SCI_STATUS scif_sas_controller_default_initialize_handler(
   SCI_BASE_CONTROLLER_T * controller
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger((SCIF_SAS_CONTROLLER_T *)controller),
      SCIF_LOG_OBJECT_CONTROLLER,
      "Controller:0x%x State:0x%x invalid state to initialize controller.\n",
      controller,
      sci_base_state_machine_get_state(
         &((SCIF_SAS_CONTROLLER_T *)controller)->parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * @brief This method provides default handling (i.e. returns an error)
 *        when a user attempts to start an IO on a controller and a start
 *        IO operation is not allowed.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             on which the user is attempting to perform a start IO
 *             operation.
 * @param[in]  remote_device This parameter specifies the remote deivce
 *             object on which the user is attempting to perform a start IO
 *             operation.
 * @param[in]  io_request This parameter specifies the IO request to be
 *             started.
 * @param[in]  io_tag This parameter specifies the optional allocated
 *             IO tag.  Please reference scif_controller_start_io() for
 *             more information.
 *
 * @return This method returns an indication that start IO operations
 *         are not allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
static
SCI_STATUS scif_sas_controller_default_start_io_handler(
   SCI_BASE_CONTROLLER_T    * controller,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request,
   U16                        io_tag
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger((SCIF_SAS_CONTROLLER_T *)controller),
      SCIF_LOG_OBJECT_CONTROLLER,
      "Controller:0x%x State:0x%x invalid state to start IO.\n",
      controller,
      sci_base_state_machine_get_state(
         &((SCIF_SAS_CONTROLLER_T *)controller)->parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * @brief This method provides default handling (i.e. returns an error)
 *        when a user attempts to complete an IO on a controller and a
 *        complete IO operation is not allowed.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             on which the user is attempting to perform a complete IO
 *             operation.
 * @param[in]  remote_device This parameter specifies the remote deivce
 *             object on which the user is attempting to perform a start IO
 *             operation.
 * @param[in]  io_request This parameter specifies the IO request to be
 *             started.
 *
 * @return This method returns an indication that complete IO operations
 *         are not allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
static
SCI_STATUS scif_sas_controller_default_complete_io_handler(
   SCI_BASE_CONTROLLER_T    * controller,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger((SCIF_SAS_CONTROLLER_T *)controller),
      SCIF_LOG_OBJECT_CONTROLLER,
      "Controller:0x%x State:0x%x invalid state to complete IO.\n",
      controller,
      sci_base_state_machine_get_state(
         &((SCIF_SAS_CONTROLLER_T *)controller)->parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * @brief This method provides default handling (i.e. returns an error)
 *        when a user attempts to continue an IO on a controller and a
 *        continue IO operation is not allowed.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             on which the user is attempting to perform a continue IO
 *             operation.
 * @param[in]  remote_device This parameter specifies the remote deivce
 *             object on which the user is attempting to perform a start IO
 *             operation.
 * @param[in]  io_request This parameter specifies the IO request to be
 *             started.
 *
 * @return This method returns an indication that continue IO operations
 *         are not allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
static
SCI_STATUS scif_sas_controller_default_continue_io_handler(
   SCI_BASE_CONTROLLER_T    * controller,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger((SCIF_SAS_CONTROLLER_T *)controller),
      SCIF_LOG_OBJECT_CONTROLLER,
      "Controller:0x%x State:0x%x invalid state to continue IO.\n",
      controller,
      sci_base_state_machine_get_state(
         &((SCIF_SAS_CONTROLLER_T *)controller)->parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * @brief This method provides default handling (i.e. returns an error)
 *        when a user attempts to start a task on a controller and a start
 *        task operation is not allowed.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             on which the user is attempting to perform a start task
 *             operation.
 * @param[in]  remote_device This parameter specifies the remote deivce
 *             object on which the user is attempting to perform a start
 *             task operation.
 * @param[in]  task_request This parameter specifies the task management
 *             request to be started.
 * @param[in]  io_tag This parameter specifies the optional allocated
 *             IO tag.  Please reference scif_controller_start_task() for
 *             more information.
 *
 * @return This method returns an indication that start task operations
 *         are not allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
static
SCI_STATUS scif_sas_controller_default_start_task_handler(
   SCI_BASE_CONTROLLER_T    * controller,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * task_request,
   U16                        io_tag
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger((SCIF_SAS_CONTROLLER_T *)controller),
      SCIF_LOG_OBJECT_CONTROLLER,
      "Controller:0x%x State:0x%x invalid state to start task mgmt.\n",
      controller,
      sci_base_state_machine_get_state(
         &((SCIF_SAS_CONTROLLER_T *)controller)->parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * @brief This method provides default handling (i.e. returns an error)
 *        when a user attempts to complete a task on a controller and a
 *        complete task operation is not allowed.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             on which the user is attempting to perform a complete task
 *             operation.
 * @param[in]  remote_device This parameter specifies the remote deivce
 *             object on which the user is attempting to perform a start
 *             task operation.
 * @param[in]  task_request This parameter specifies the task management
 *             request to be started.
 *
 * @return This method returns an indication that complete task operations
 *         are not allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
static
SCI_STATUS scif_sas_controller_default_complete_task_handler(
   SCI_BASE_CONTROLLER_T    * controller,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * task_request
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger((SCIF_SAS_CONTROLLER_T *)controller),
      SCIF_LOG_OBJECT_CONTROLLER,
      "Controller:0x%x State:0x%x invalid state to complete task mgmt.\n",
      controller,
      sci_base_state_machine_get_state(
         &((SCIF_SAS_CONTROLLER_T *)controller)->parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

static
SCI_STATUS scif_sas_controller_failed_state_start_io_handler(
   SCI_BASE_CONTROLLER_T    * controller,
   SCI_BASE_REMOTE_DEVICE_T * remote_device,
   SCI_BASE_REQUEST_T       * io_request,
   U16                        io_tag
)
{
   SCIF_LOG_WARNING((
      sci_base_object_get_logger((SCIF_SAS_CONTROLLER_T *)controller),
      SCIF_LOG_OBJECT_CONTROLLER,
      "Controller:0x%x State:0x%x invalid state to start IO.\n",
      controller,
      sci_base_state_machine_get_state(
         &((SCIF_SAS_CONTROLLER_T *)controller)->parent.state_machine)
   ));

   return SCI_FAILURE;
}

#define scif_sas_controller_stopping_complete_io_handler   \
        scif_sas_controller_ready_complete_io_handler
#define scif_sas_controller_stopping_complete_task_handler \
        scif_sas_controller_ready_complete_task_handler
#define scif_sas_controller_default_start_high_priority_io_handler \
        scif_sas_controller_default_start_io_handler
#define scif_sas_controller_default_complete_high_priority_io_handler \
        scif_sas_controller_default_complete_io_handler
#define scif_sas_controller_stopping_complete_high_priority_io_handler \
        scif_sas_controller_ready_complete_high_priority_io_handler


SCI_BASE_CONTROLLER_STATE_HANDLER_T
   scif_sas_controller_state_handler_table[SCI_BASE_CONTROLLER_MAX_STATES] =
{
   // SCI_BASE_CONTROLLER_STATE_INITIAL
   {
      scif_sas_controller_default_start_handler,
      scif_sas_controller_default_stop_handler,
      scif_sas_controller_default_reset_handler,
      scif_sas_controller_default_initialize_handler,
      scif_sas_controller_default_start_io_handler,
      scif_sas_controller_default_start_high_priority_io_handler,
      scif_sas_controller_default_complete_io_handler,
      scif_sas_controller_default_complete_high_priority_io_handler,
      scif_sas_controller_default_continue_io_handler,
      scif_sas_controller_default_start_task_handler,
      scif_sas_controller_default_complete_task_handler
   },
   // SCI_BASE_CONTROLLER_STATE_RESET
   {
      scif_sas_controller_default_start_handler,
      scif_sas_controller_default_stop_handler,
      scif_sas_controller_default_reset_handler,
      scif_sas_controller_reset_initialize_handler,
      scif_sas_controller_default_start_io_handler,
      scif_sas_controller_default_start_high_priority_io_handler,
      scif_sas_controller_default_complete_io_handler,
      scif_sas_controller_default_complete_high_priority_io_handler,
      scif_sas_controller_default_continue_io_handler,
      scif_sas_controller_default_start_task_handler,
      scif_sas_controller_default_complete_task_handler
   },
   // SCI_BASE_CONTROLLER_STATE_INITIALIZING
   {
      scif_sas_controller_default_start_handler,
      scif_sas_controller_default_stop_handler,
      scif_sas_controller_default_reset_handler,
      scif_sas_controller_default_initialize_handler,
      scif_sas_controller_default_start_io_handler,
      scif_sas_controller_default_start_high_priority_io_handler,
      scif_sas_controller_default_complete_io_handler,
      scif_sas_controller_default_complete_high_priority_io_handler,
      scif_sas_controller_default_continue_io_handler,
      scif_sas_controller_default_start_task_handler,
      scif_sas_controller_default_complete_task_handler
   },
   // SCI_BASE_CONTROLLER_STATE_INITIALIZED
   {
      scif_sas_controller_initialized_start_handler,
      scif_sas_controller_default_stop_handler,
      scif_sas_controller_default_reset_handler,
      scif_sas_controller_default_initialize_handler,
      scif_sas_controller_default_start_io_handler,
      scif_sas_controller_default_start_high_priority_io_handler,
      scif_sas_controller_default_complete_io_handler,
      scif_sas_controller_default_complete_high_priority_io_handler,
      scif_sas_controller_default_continue_io_handler,
      scif_sas_controller_default_start_task_handler,
      scif_sas_controller_default_complete_task_handler
   },
   // SCI_BASE_CONTROLLER_STATE_STARTING
   {
      scif_sas_controller_default_start_handler,
      scif_sas_controller_default_stop_handler,
      scif_sas_controller_default_reset_handler,
      scif_sas_controller_default_initialize_handler,
      scif_sas_controller_default_start_io_handler,
      scif_sas_controller_default_start_high_priority_io_handler,
      scif_sas_controller_default_complete_io_handler,
      scif_sas_controller_default_complete_high_priority_io_handler,
      scif_sas_controller_default_continue_io_handler,
      scif_sas_controller_default_start_task_handler,
      scif_sas_controller_default_complete_task_handler
   },
   // SCI_BASE_CONTROLLER_STATE_READY
   {
      scif_sas_controller_default_start_handler,
      scif_sas_controller_ready_stop_handler,
      scif_sas_controller_ready_reset_handler,
      scif_sas_controller_default_initialize_handler,
      scif_sas_controller_ready_start_io_handler,
      scif_sas_controller_ready_start_high_priority_io_handler,
      scif_sas_controller_ready_complete_io_handler,
      scif_sas_controller_ready_complete_high_priority_io_handler,
      scif_sas_controller_ready_continue_io_handler,
      scif_sas_controller_ready_start_task_handler,
      scif_sas_controller_ready_complete_task_handler
   },
   // SCI_BASE_CONTROLLER_STATE_RESETTING
   {
      scif_sas_controller_default_start_handler,
      scif_sas_controller_default_stop_handler,
      scif_sas_controller_default_reset_handler,
      scif_sas_controller_default_initialize_handler,
      scif_sas_controller_default_start_io_handler,
      scif_sas_controller_default_start_high_priority_io_handler,
      scif_sas_controller_default_complete_io_handler,
      scif_sas_controller_default_complete_high_priority_io_handler,
      scif_sas_controller_default_continue_io_handler,
      scif_sas_controller_default_start_task_handler,
      scif_sas_controller_default_complete_task_handler
   },
   // SCI_BASE_CONTROLLER_STATE_STOPPING
   {
      scif_sas_controller_default_start_handler,
      scif_sas_controller_default_stop_handler,
      scif_sas_controller_default_reset_handler,
      scif_sas_controller_default_initialize_handler,
      scif_sas_controller_default_start_io_handler,
      scif_sas_controller_stopping_start_high_priority_io_handler,
      scif_sas_controller_stopping_complete_io_handler,
      scif_sas_controller_stopping_complete_high_priority_io_handler,
      scif_sas_controller_default_continue_io_handler,
      scif_sas_controller_default_start_task_handler, /**@todo Allow in core?*/
      scif_sas_controller_stopping_complete_task_handler
   },
   // SCI_BASE_CONTROLLER_STATE_STOPPED
   {
      scif_sas_controller_default_start_handler,
      scif_sas_controller_default_stop_handler,
      scif_sas_controller_stopped_reset_handler,
      scif_sas_controller_default_initialize_handler,
      scif_sas_controller_default_start_io_handler,
      scif_sas_controller_default_start_high_priority_io_handler,
      scif_sas_controller_default_complete_io_handler,
      scif_sas_controller_default_complete_high_priority_io_handler,
      scif_sas_controller_default_continue_io_handler,
      scif_sas_controller_default_start_task_handler,
      scif_sas_controller_default_complete_task_handler
   },
   // SCI_BASE_CONTROLLER_STATE_FAILED
   {
      scif_sas_controller_default_start_handler,
      scif_sas_controller_default_stop_handler,
      scif_sas_controller_failed_reset_handler,
      scif_sas_controller_default_initialize_handler,
      scif_sas_controller_failed_state_start_io_handler,
      scif_sas_controller_failed_state_start_io_handler,
      scif_sas_controller_default_complete_io_handler,
      scif_sas_controller_default_complete_high_priority_io_handler,
      scif_sas_controller_default_continue_io_handler,
      scif_sas_controller_default_start_task_handler,
      scif_sas_controller_default_complete_task_handler
   }
};

