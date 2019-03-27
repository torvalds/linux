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
 * @brief This file contains all of the entrance and exit methods for each
 *        of the domain states defined by the SCI_BASE_DOMAIN state
 *        machine.
 */

#include <dev/isci/scil/intel_sas.h>
#include <dev/isci/scil/scic_port.h>

#include <dev/isci/scil/scif_sas_logger.h>
#include <dev/isci/scil/scif_sas_domain.h>
#include <dev/isci/scil/scif_sas_controller.h>
#include <dev/isci/scil/scic_controller.h>

//******************************************************************************
//* P R O T E C T E D    M E T H O D S
//******************************************************************************

/**
 * @brief This method will attempt to transition to the stopped state.
 *        The transition will only occur if the criteria for transition is
 *        met (i.e. all IOs are complete and all devices are stopped).
 *
 * @param[in]  fw_domain This parameter specifies the domain in which to
 *             to attempt to perform the transition.
 *
 * @return none
 */
void scif_sas_domain_transition_to_stopped_state(
   SCIF_SAS_DOMAIN_T * fw_domain
)
{
   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_domain_transition_to_stopped_state(0x%x) enter\n",
      fw_domain
   ));

   // If IOs are quiesced, and all remote devices are stopped,
   // then transition directly to the STOPPED state.
   if (  (fw_domain->request_list.element_count == 0)
      && (fw_domain->device_start_count == 0) )
   {
      SCIF_LOG_INFO((
         sci_base_object_get_logger(fw_domain),
         SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
         "Domain:0x%x immediate transition to STOPPED\n",
         fw_domain
      ));

      sci_base_state_machine_change_state(
         &fw_domain->parent.state_machine, SCI_BASE_DOMAIN_STATE_STOPPED
      );
   }
}


/**
 * @brief This method is called upon entrance to all states where the
 *        previous state may have been the DISCOVERING state.
 *        We issue the scif_cb_domain_discovery_complete() notification
 *        from this method, assuming pre-requisites are met, as opposed
 *        to in the exit handler of the DISCOVERING state, so that the
 *        appropriate state handlers are in place should the user decide
 *        to call scif_domain_discover() again.
 *
 * @param[in]  fw_domain This parameter specifies the domain for which
 *             the state transition has occurred.
 *
 * @return none
 */
static
void scif_sas_domain_transition_from_discovering_state(
   SCIF_SAS_DOMAIN_T * fw_domain
)
{
   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_domain_transition_from_discovering_state(0x%x) enter\n",
      fw_domain
   ));

   if (fw_domain->parent.state_machine.previous_state_id
       == SCI_BASE_DOMAIN_STATE_DISCOVERING)
   {
      scif_sas_controller_restore_interrupt_coalescence(fw_domain->controller);

      scif_cb_timer_stop(fw_domain->controller, fw_domain->operation.timer);

      scif_cb_domain_discovery_complete(
         fw_domain->controller, fw_domain, fw_domain->operation.status
      );
   }
}


/**
 * @brief This method is called upon entrance to DISCOVERING state. Right before
 *           transitioning to DISCOVERING state, we temporarily change interrupt
 *           coalescence scheme.
 *
 * @param[in]  fw_domain This parameter specifies the domain for which
 *             the state transition has occurred.
 *
 * @return none
 */
void scif_sas_domain_transition_to_discovering_state(
   SCIF_SAS_DOMAIN_T * fw_domain
)
{
   scif_sas_controller_save_interrupt_coalescence(fw_domain->controller);

   sci_base_state_machine_change_state(
      &fw_domain->parent.state_machine, SCI_BASE_DOMAIN_STATE_DISCOVERING
   );
}


/**
 * @brief This method implements the actions taken when entering the
 *        INITIAL state.
 *
 * @param[in]  object This parameter specifies the base object for which
 *             the state transition is occurring.  This is cast into a
 *             SCIF_SAS_DOMAIN object in the method implementation.
 *
 * @return none
 */
static
void scif_sas_domain_initial_state_enter(
   SCI_BASE_OBJECT_T * object
)
{
   SCIF_SAS_DOMAIN_T * fw_domain = (SCIF_SAS_DOMAIN_T *)object;

   SET_STATE_HANDLER(
      fw_domain,
      scif_sas_domain_state_handler_table,
      SCI_BASE_DOMAIN_STATE_INITIAL
   );

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_domain),
      SCIF_LOG_OBJECT_DOMAIN,
      "scif_sas_domain_initial_state_enter(0x%x) enter\n",
      fw_domain
   ));
}

/**
 * @brief This method implements the actions taken when entering the
 *        STARTING state.  This includes setting the state handlers and
 *        checking to see if the core port has already become READY.
 *
 * @param[in]  object This parameter specifies the base object for which
 *             the state transition is occurring.  This is cast into a
 *             SCIF_SAS_DOMAIN object in the method implementation.
 *
 * @return none
 */
static
void scif_sas_domain_starting_state_enter(
   SCI_BASE_OBJECT_T * object
)
{
   SCIF_SAS_DOMAIN_T * fw_domain = (SCIF_SAS_DOMAIN_T *)object;

   SET_STATE_HANDLER(
      fw_domain,
      scif_sas_domain_state_handler_table,
      SCI_BASE_DOMAIN_STATE_STARTING
   );

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_domain_starting_state_enter(0x%x) enter\n",
      fw_domain
   ));

   scif_sas_domain_transition_from_discovering_state(fw_domain);

   // If we entered the STARTING state and the core port is actually ready,
   // then directly transition into the READY state.  This can occur
   // if we were in the middle of discovery when the port failed
   // (causing a transition to STOPPING), then before reaching STOPPED
   // the port becomes ready again.
   if (fw_domain->is_port_ready == TRUE)
   {
      sci_base_state_machine_change_state(
         &fw_domain->parent.state_machine, SCI_BASE_DOMAIN_STATE_READY
      );
   }
}

/**
 * @brief This method implements the actions taken when entering the
 *        READY state.  If the transition into this state came from:
 *        - the STARTING state, then alert the user via a
 *          scif_cb_domain_change_notification() that the domain
 *          has at least 1 device ready for discovery.
 *        - the DISCOVERING state, then alert the user that
 *          discovery is complete via the
 *          scif_cb_domain_discovery_complete() notification that
 *          discovery is finished.
 *
 * @param[in]  object This parameter specifies the base object for which
 *             the state transition is occurring.  This is cast into a
 *             SCIF_SAS_DOMAIN object in the method implementation.
 *
 * @return none
 */
static
void scif_sas_domain_ready_state_enter(
   SCI_BASE_OBJECT_T * object
)
{
   SCIF_SAS_DOMAIN_T * fw_domain = (SCIF_SAS_DOMAIN_T *)object;

   SET_STATE_HANDLER(
      fw_domain,
      scif_sas_domain_state_handler_table,
      SCI_BASE_DOMAIN_STATE_READY
   );

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_domain_ready_state_enter(0x%x) enter\n",
      fw_domain
   ));

   if (fw_domain->parent.state_machine.previous_state_id
       == SCI_BASE_DOMAIN_STATE_STARTING)
   {
      scif_cb_domain_ready(fw_domain->controller, fw_domain);

      // Only indicate the domain change notification if the previous
      // state was the STARTING state.  We issue the notification here
      // as opposed to exit of the STARTING state so that the appropriate
      // state handlers are in place should the user call
      // scif_domain_discover() from scif_cb_domain_change_notification()
      scif_cb_domain_change_notification(fw_domain->controller, fw_domain);
   }
   else if (fw_domain->parent.state_machine.previous_state_id
            == SCI_BASE_DOMAIN_STATE_DISCOVERING)
   {
      //if domain discovery timed out, we will NOT go back to discover even
      //the broadcast change count is not zero. Instead we finish the discovery
      //back to user. User can check the operation status and decide to
      //retry discover all over again.
      if (fw_domain->operation.status == SCI_FAILURE_TIMEOUT)
         fw_domain->broadcast_change_count = 0;

      // Check the broadcast change count to determine if discovery
      // is indeed complete.
      if (fw_domain->broadcast_change_count == 0)
      {
         scif_sas_domain_transition_from_discovering_state(fw_domain);
         scif_cb_domain_ready(fw_domain->controller, fw_domain);
      }
      else
      {
         // The broadcast change count indicates something my have
         // changed in the domain, while a discovery was ongoing.
         // Thus, we should start discovery over again.
         sci_base_state_machine_change_state(
            &fw_domain->parent.state_machine, SCI_BASE_DOMAIN_STATE_DISCOVERING
         );
      }

      // Enable the BCN because underneath hardware may disabled any further
      // BCN.
      scic_port_enable_broadcast_change_notification(fw_domain->core_object);
   }
}

/**
 * @brief This method implements the actions taken when exiting the
 *        READY state.
 *
 * @param[in]  object This parameter specifies the base object for which
 *             the state transition is occurring.  This is cast into a
 *             SCIF_SAS_DOMAIN object in the method implementation.
 *
 * @return none
 */
static
void scif_sas_domain_ready_state_exit(
   SCI_BASE_OBJECT_T * object
)
{
   SCIF_SAS_DOMAIN_T * fw_domain = (SCIF_SAS_DOMAIN_T *)object;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_domain_ready_state_exit(0x%x) enter\n",
      fw_domain
   ));

   scif_cb_domain_not_ready(fw_domain->controller, fw_domain);
}

/**
 * @brief This method implements the actions taken when entering the
 *        STOPPING state.
 *
 * @param[in]  object This parameter specifies the base object for which
 *             the state transition is occurring.  This is cast into a
 *             SCIF_SAS_DOMAIN object in the method implementation.
 *
 * @return none
 */
static
void scif_sas_domain_stopping_state_enter(
   SCI_BASE_OBJECT_T * object
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device;
   SCIF_SAS_DOMAIN_T        * fw_domain = (SCIF_SAS_DOMAIN_T *)object;
   SCI_ABSTRACT_ELEMENT_T   * element   = sci_abstract_list_get_front(
                                             &fw_domain->remote_device_list
                                          );

   SET_STATE_HANDLER(
      fw_domain,
      scif_sas_domain_state_handler_table,
      SCI_BASE_DOMAIN_STATE_STOPPING
   );

   // This must be invoked after the state handlers are set to ensure
   // appropriate processing will occur if the user attempts to perform
   // additional actions.
   scif_sas_domain_transition_from_discovering_state(fw_domain);

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_domain_stopping_state_enter(0x%x) enter\n",
      fw_domain
   ));

   scif_sas_high_priority_request_queue_purge_domain(
      &fw_domain->controller->hprq, fw_domain
   );

   // Search the domain's list of devices and put them all in the STOPPING
   // state.
   while (element != NULL)
   {
      fw_device = (SCIF_SAS_REMOTE_DEVICE_T*)
                  sci_abstract_list_get_object(element);

      // This method will stop the core device.  The core will terminate
      // all IO requests currently outstanding.
      fw_device->state_handlers->parent.stop_handler(&fw_device->parent);

      element = sci_abstract_list_get_next(element);
   }

   // Attempt to transition to the stopped state.
   scif_sas_domain_transition_to_stopped_state(fw_domain);
}

/**
 * @brief This method implements the actions taken when entering the
 *        STOPPED state.
 *
 * @param[in]  object This parameter specifies the base object for which
 *             the state transition is occurring.  This is cast into a
 *             SCIF_SAS_DOMAIN object in the method implementation.
 *
 * @return none
 */
static
void scif_sas_domain_stopped_state_enter(
   SCI_BASE_OBJECT_T * object
)
{
   SCIF_SAS_DOMAIN_T * fw_domain = (SCIF_SAS_DOMAIN_T *)object;

   SET_STATE_HANDLER(
      fw_domain,
      scif_sas_domain_state_handler_table,
      SCI_BASE_DOMAIN_STATE_STOPPED
   );

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_domain_stopped_state_enter(0x%x) enter\n",
      fw_domain
   ));

   // A hot unplug of the direct attached device has occurred.  Thus,
   // notify the user. Note, if the controller is not in READY state,
   // mostly likely the controller is in STOPPING or STOPPED state,
   // meaning the controller is in the process of stopping, we should
   // not call back to user in the middle of controller stopping.
   if(fw_domain->controller->parent.state_machine.current_state_id
         == SCI_BASE_CONTROLLER_STATE_READY)
      scif_cb_domain_change_notification(fw_domain->controller, fw_domain);
}

/**
 * @brief This method implements the actions taken when entering the
 *        DISCOVERING state.  This includes determining from which
 *        state we entered.  If we entered from stopping that some sort
 *        of hot-remove of the port occurred.  In the hot-remove case
 *        all devices should be in the STOPPED state already and, as
 *        a result, are removed from the domain with a notification sent
 *        to the framework user.
 *
 * @note This method currently only handles hot-insert/hot-remove of
 *       direct attached SSP devices.
 *
 * @param[in]  object This parameter specifies the base object for which
 *             the state transition is occurring.  This is cast into a
 *             SCIF_SAS_DOMAIN object in the method implementation.
 *
 * @return none
 */
static
void scif_sas_domain_discovering_state_enter(
   SCI_BASE_OBJECT_T * object
)
{
   SCIF_SAS_DOMAIN_T * fw_domain = (SCIF_SAS_DOMAIN_T *)object;

   SET_STATE_HANDLER(
      fw_domain,
      scif_sas_domain_state_handler_table,
      SCI_BASE_DOMAIN_STATE_DISCOVERING
   );

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_domain),
      SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_domain_discovering_state_enter(0x%x) enter\n",
      fw_domain
   ));

   fw_domain->broadcast_change_count = 0;

   // Did the domain just go through a port not ready action?  If it did,
   // then we will be entering from the STOPPED state.
   if (fw_domain->parent.state_machine.previous_state_id
       != SCI_BASE_DOMAIN_STATE_STOPPED)
   {
      SCIF_SAS_REMOTE_DEVICE_T * remote_device;
      SCIC_PORT_PROPERTIES_T     properties;

      scic_port_get_properties(fw_domain->core_object, &properties);

      // If the device has not yet been added to the domain, then
      // inform the user that the device is new.
      remote_device = (SCIF_SAS_REMOTE_DEVICE_T *)
                      scif_domain_get_device_by_sas_address(
                         fw_domain, &properties.remote.sas_address
                      );
      if (remote_device == SCI_INVALID_HANDLE)
      {
         // simply notify the user of the new DA device and be done
         // with discovery.
         scif_cb_domain_da_device_added(
            fw_domain->controller,
            fw_domain,
            &properties.remote.sas_address,
            &properties.remote.protocols
         );
      }
      else
      {
         if(properties.remote.protocols.u.bits.smp_target)
            //kick off the smp discover process.
            scif_sas_domain_start_smp_discover(fw_domain, remote_device);
      }
   }
   else  //entered from STOPPED state.
   {
      SCI_ABSTRACT_ELEMENT_T * current_element =
             sci_abstract_list_get_front(&(fw_domain->remote_device_list) );

      SCIF_SAS_REMOTE_DEVICE_T * fw_device;

      while (current_element != NULL)
      {
         fw_device = (SCIF_SAS_REMOTE_DEVICE_T *)
                     sci_abstract_list_get_object(current_element);

         ASSERT(fw_device->parent.state_machine.current_state_id
                == SCI_BASE_REMOTE_DEVICE_STATE_STOPPED);

         current_element =
            sci_abstract_list_get_next(current_element);

         SCIF_LOG_INFO((
            sci_base_object_get_logger(fw_domain),
            SCIF_LOG_OBJECT_DOMAIN | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
            "Controller:0x%x Domain:0x%x Device:0x%x removed\n",
            fw_domain->controller, fw_domain, fw_device
         ));

         // Notify the framework user of the device removal.
         scif_cb_domain_device_removed(
            fw_domain->controller, fw_domain, fw_device
         );
      }

      ASSERT(fw_domain->request_list.element_count == 0);
      ASSERT(sci_abstract_list_size(&fw_domain->remote_device_list) == 0);

      sci_base_state_machine_change_state(
         &fw_domain->parent.state_machine, SCI_BASE_DOMAIN_STATE_STARTING
      );
   }
}

SCI_BASE_STATE_T scif_sas_domain_state_table[SCI_BASE_DOMAIN_MAX_STATES] =
{
   {
      SCI_BASE_DOMAIN_STATE_INITIAL,
      scif_sas_domain_initial_state_enter,
      NULL,
   },
   {
      SCI_BASE_DOMAIN_STATE_STARTING,
      scif_sas_domain_starting_state_enter,
      NULL,
   },
   {
      SCI_BASE_DOMAIN_STATE_READY,
      scif_sas_domain_ready_state_enter,
      scif_sas_domain_ready_state_exit,
   },
   {
      SCI_BASE_DOMAIN_STATE_STOPPING,
      scif_sas_domain_stopping_state_enter,
      NULL,
   },
   {
      SCI_BASE_DOMAIN_STATE_STOPPED,
      scif_sas_domain_stopped_state_enter,
      NULL,
   },
   {
      SCI_BASE_DOMAIN_STATE_DISCOVERING,
      scif_sas_domain_discovering_state_enter,
      NULL,
   }
};

