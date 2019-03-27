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
 * @brief
 */

#include <dev/isci/scil/scic_remote_device.h>

#include <dev/isci/scil/scif_sas_remote_device.h>
#include <dev/isci/scil/scif_sas_domain.h>
#include <dev/isci/scil/scif_sas_logger.h>


/**
 * This constant indicates the number of milliseconds to wait for the core
 * to start/stop it's remote device object.
 */
//#define SCIF_SAS_REMOTE_DEVICE_CORE_OP_TIMEOUT 1000

//******************************************************************************
//* P R O T E C T E D   M E T H O D S
//******************************************************************************

/**
 * @brief This method implements the actions taken when entering the
 *        INITIAL state.  This basically, causes an immediate transition
 *        into the STOPPED state.
 *
 * @param[in]  object This parameter specifies the base object for which
 *             the state transition is occurring.  This is cast into a
 *             SCIF_SAS_REMOTE_DEVICE object in the method implementation.
 *
 * @return none
 */
static
void scif_sas_remote_device_initial_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T *)object;

   SET_STATE_HANDLER(
      fw_device,
      scif_sas_remote_device_state_handler_table,
      SCI_BASE_REMOTE_DEVICE_STATE_INITIAL
   );

   // Initial state is a transitional state to the stopped state
   sci_base_state_machine_change_state(
      &fw_device->parent.state_machine,
      SCI_BASE_REMOTE_DEVICE_STATE_STOPPED
   );
}

/**
 * @brief This method implements the actions taken when entering the
 *        STOPPED state.  This method updates the domains count of started
 *        devices and will invoke the destruct method if this entrance into
 *        the STOPPED state was due to a scif_remote_device_destruct()
 *        call by the user.
 *
 * @param[in]  object This parameter specifies the base object for which
 *             the state transition is occurring.  This is cast into a
 *             SCIF_SAS_REMOTE_DEVICE object in the method implementation.
 *
 * @return none
 */
static
void scif_sas_remote_device_stopped_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T *)object;

   SET_STATE_HANDLER(
      fw_device,
      scif_sas_remote_device_state_handler_table,
      SCI_BASE_REMOTE_DEVICE_STATE_STOPPED
   );

   // There should be no outstanding requests for this device in the
   // stopped state.
   ASSERT(fw_device->request_count == 0);

   // If we are entering the stopped state as a result of a destruct
   // request, then let's perform the actual destruct operation now.
   if (fw_device->destruct_when_stopped == TRUE)
      fw_device->operation_status
         = fw_device->state_handlers->parent.destruct_handler(
              &fw_device->parent
           );

   /// @todo What should we do if this call fails?
   fw_device->domain->state_handlers->device_stop_complete_handler(
      &fw_device->domain->parent, &fw_device->parent
   );
}

/**
 * @brief This method implements the actions taken when entering the
 *        STARTING state.  This method will attempt to start the core
 *        remote device and will kick-start the starting sub-state machine
 *        if no errors are encountered.
 *
 * @param[in]  object This parameter specifies the base object for which
 *             the state transition is occurring.  This is cast into a
 *             SCIF_SAS_REMOTE_DEVICE object in the method implementation.
 *
 * @return none
 */
static
void scif_sas_remote_device_starting_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T *)object;

   SET_STATE_HANDLER(
      fw_device,
      scif_sas_remote_device_state_handler_table,
      SCI_BASE_REMOTE_DEVICE_STATE_STARTING
   );

   SCIF_LOG_INFO((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_REMOTE_DEVICE_CONFIG,
      "RemoteDevice:0x%x starting/configuring\n",
      fw_device
   ));

   fw_device->destination_state =
      SCIF_SAS_REMOTE_DEVICE_DESTINATION_STATE_READY;

   sci_base_state_machine_start(&fw_device->starting_substate_machine);

   fw_device->operation_status = scic_remote_device_start(
                                    fw_device->core_object,
                                    SCIF_SAS_REMOTE_DEVICE_CORE_OP_TIMEOUT
                                 );

   if (fw_device->operation_status != SCI_SUCCESS)
   {
      fw_device->state_handlers->parent.fail_handler(&fw_device->parent);

      // Something is seriously wrong.  Starting the core remote device
      // shouldn't fail in anyway in this state.
      scif_cb_controller_error(fw_device->domain->controller,
              SCI_CONTROLLER_REMOTE_DEVICE_ERROR);
   }
}

/**
 * @brief This method implements the actions taken when exiting the
 *        STARTING state.  Currently this method simply stops the
 *        sub-state machine.
 *
 * @param[in]  object This parameter specifies the base object for which
 *             the state transition is occurring.  This is cast into a
 *             SCIF_SAS_REMOTE_DEVICE object in the method implementation.
 *
 * @return none
 */
static
void scif_sas_remote_device_starting_state_exit(
   SCI_BASE_OBJECT_T *object
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T *)object;

   fw_device->destination_state =
      SCIF_SAS_REMOTE_DEVICE_DESTINATION_STATE_UNSPECIFIED;

   // Transition immediately into the operational sub-state.
   sci_base_state_machine_stop(&fw_device->starting_substate_machine);
}

/**
 * @brief This method implements the actions taken when entering the
 *        READY state.  Currently this method simply starts the
 *        sub-state machine.
 *
 * @param[in]  object This parameter specifies the base object for which
 *             the state transition is occurring.  This is cast into a
 *             SCIF_SAS_REMOTE_DEVICE object in the method implementation.
 *
 * @return none
 */
static
void scif_sas_remote_device_ready_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T *)object;

   // Transition immediately into the operational sub-state.
   sci_base_state_machine_start(&fw_device->ready_substate_machine);

#if defined(DISABLE_WIDE_PORTED_TARGETS)
   scif_sas_domain_remote_device_start_complete(fw_device->domain,fw_device);
#endif
}

/**
 * @brief This method implements the actions taken when exiting the
 *        READY state.  Currently this method simply stops the
 *        sub-state machine.
 *
 * @param[in]  object This parameter specifies the base object for which
 *             the state transition is occurring.  This is cast into a
 *             SCIF_SAS_REMOTE_DEVICE object in the method implementation.
 *
 * @return none
 */
static
void scif_sas_remote_device_ready_state_exit(
   SCI_BASE_OBJECT_T *object
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T *)object;

   // Transition immediately into the operational sub-state.
   sci_base_state_machine_stop(&fw_device->ready_substate_machine);
}

/**
 * @brief This method implements the actions taken when entering the
 *        STOPPING state.  This includes: stopping the core remote device
 *        and handling any errors that may occur.
 *
 * @param[in]  object This parameter specifies the base object for which
 *             the state transition is occurring.  This is cast into a
 *             SCIF_SAS_REMOTE_DEVICE object in the method implementation.
 *
 * @return none
 */
static
void scif_sas_remote_device_stopping_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T *)object;

   SET_STATE_HANDLER(
      fw_device,
      scif_sas_remote_device_state_handler_table,
      SCI_BASE_REMOTE_DEVICE_STATE_STOPPING
   );

   fw_device->operation_status = scic_remote_device_stop(
                                    fw_device->core_object,
                                    SCIF_SAS_REMOTE_DEVICE_CORE_OP_TIMEOUT
                                 );

   // If there was a failure, then transition directly to the stopped state.
   if (fw_device->operation_status != SCI_SUCCESS)
   {
      /**
       * @todo We may want to consider adding handling to reset the
       *       structure data for the framework and core devices here
       *       in order to help aid recovery.
       */

      fw_device->state_handlers->stop_complete_handler(
         fw_device, fw_device->operation_status
      );
   }
}

/**
 * @brief This method implements the actions taken when exiting the
 *        STOPPING state.
 *
 * @param[in]  object This parameter specifies the base object for which
 *             the state transition is occurring.  This is cast into a
 *             SCIF_SAS_REMOTE_DEVICE object in the method implementation.
 *
 * @return none
 */
static
void scif_sas_remote_device_stopping_state_exit(
   SCI_BASE_OBJECT_T *object
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T *)object;

   // Let the domain know that the device has stopped
   fw_device->domain->device_start_count--;
}

/**
 * @brief This method implements the actions taken when entering the
 *        FAILED state.  This includes setting the state handler methods
 *        and issuing a scif_cb_remote_device_failed() notification to
 *        the user.
 *
 * @param[in]  object This parameter specifies the base object for which
 *             the state transition is occurring.  This is cast into a
 *             SCIF_SAS_REMOTE_DEVICE object in the method implementation.
 *
 * @return none
 */
static
void scif_sas_remote_device_failed_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T *)object;

   SET_STATE_HANDLER(
      fw_device,
      scif_sas_remote_device_state_handler_table,
      SCI_BASE_REMOTE_DEVICE_STATE_FAILED
   );

   SCIF_LOG_INFO((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_REMOTE_DEVICE_CONFIG,
      "Domain:0x%x Device:0x%x Status:0x%x device failed\n",
      fw_device->domain, fw_device, fw_device->operation_status
   ));

   // Notify the user that the device has failed.
   scif_cb_remote_device_failed(
      fw_device->domain->controller,
      fw_device->domain,
      fw_device,
      fw_device->operation_status
   );

   // Only call start_complete for the remote device if the device failed
   // from the STARTING state.
   if (fw_device->parent.state_machine.previous_state_id
       == SCI_BASE_REMOTE_DEVICE_STATE_STARTING)
      scif_sas_domain_remote_device_start_complete(fw_device->domain,fw_device);
}

/**
 * @brief This method implements the actions taken when entering the RESETTING
 *        state.
 *
 * @param[in]  object This parameter specifies the base object for which
 *             the state transition is occurring.  This is cast into a
 *             SCIF_SAS_REMOTE_DEVICE object in the method implementation.
 *
 * @return none
 */
static
void scif_sas_remote_device_resetting_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
}

#if !defined(DISABLE_WIDE_PORTED_TARGETS)
/**
 * @brief This method implements the actions taken when entering the UPDATING
 *        PORT WIDTH state.
 *
 * @param[in]  object This parameter specifies the base object for which
 *             the state transition is occurring.  This is cast into a
 *             SCIF_SAS_REMOTE_DEVICE object in the method implementation.
 *
 * @return none
 */
static
void scif_sas_remote_device_updating_port_width_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T *)object;

   SET_STATE_HANDLER(
      fw_device,
      scif_sas_remote_device_state_handler_table,
      SCI_BASE_REMOTE_DEVICE_STATE_UPDATING_PORT_WIDTH
   );

   fw_device->destination_state = SCIF_SAS_REMOTE_DEVICE_DESTINATION_STATE_READY;

   //If the request count is zero, go ahead to update the RNC.
   //If not, don't do anything for now. The IO complete handler of this state
   //will update the RNC whenever the request count goes down to zero.
   if (fw_device->request_count == 0)
   {
      //stop the device, upon the stop complete callback, start the device again
      //with the updated port width.
      scic_remote_device_stop(
         fw_device->core_object, SCIF_SAS_REMOTE_DEVICE_CORE_OP_TIMEOUT);
   }
}


/**
 * @brief This method implements the actions taken when exiting the
 *        STOPPING state.
 *
 * @param[in]  object This parameter specifies the base object for which
 *             the state transition is occurring.  This is cast into a
 *             SCIF_SAS_REMOTE_DEVICE object in the method implementation.
 *
 * @return none
 */
static
void scif_sas_remote_device_updating_port_width_state_exit(
   SCI_BASE_OBJECT_T *object
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T *)object;

   fw_device->destination_state =
      SCIF_SAS_REMOTE_DEVICE_DESTINATION_STATE_UNSPECIFIED;
}


#endif //#if !defined(DISABLE_WIDE_PORTED_TARGETS)

/**
 * @brief This method implements the actions taken when entering the
 *        FINAL state.  This includes setting the FINAL state handler
 *        methods.
 *
 * @param[in]  object This parameter specifies the base object for which
 *             the state transition is occurring.  This is cast into a
 *             SCIF_SAS_REMOTE_DEVICE object in the method implementation.
 *
 * @return none
 */
static
void scif_sas_remote_device_final_state_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T *)object;

   SET_STATE_HANDLER(
      fw_device,
      scif_sas_remote_device_state_handler_table,
      SCI_BASE_REMOTE_DEVICE_STATE_FINAL
   );
}


SCI_BASE_STATE_T
   scif_sas_remote_device_state_table[SCI_BASE_REMOTE_DEVICE_MAX_STATES] =
{
   {
      SCI_BASE_REMOTE_DEVICE_STATE_INITIAL,
      scif_sas_remote_device_initial_state_enter,
      NULL
   },
   {
      SCI_BASE_REMOTE_DEVICE_STATE_STOPPED,
      scif_sas_remote_device_stopped_state_enter,
      NULL
   },
   {
      SCI_BASE_REMOTE_DEVICE_STATE_STARTING,
      scif_sas_remote_device_starting_state_enter,
      scif_sas_remote_device_starting_state_exit
   },
   {
      SCI_BASE_REMOTE_DEVICE_STATE_READY,
      scif_sas_remote_device_ready_state_enter,
      scif_sas_remote_device_ready_state_exit
   },
   {
      SCI_BASE_REMOTE_DEVICE_STATE_STOPPING,
      scif_sas_remote_device_stopping_state_enter,
      scif_sas_remote_device_stopping_state_exit
   },
   {
      SCI_BASE_REMOTE_DEVICE_STATE_FAILED,
      scif_sas_remote_device_failed_state_enter,
      NULL
   },
   {
      SCI_BASE_REMOTE_DEVICE_STATE_RESETTING,
      scif_sas_remote_device_resetting_state_enter,
      NULL
   },
#if !defined(DISABLE_WIDE_PORTED_TARGETS)
   {
      SCI_BASE_REMOTE_DEVICE_STATE_UPDATING_PORT_WIDTH,
      scif_sas_remote_device_updating_port_width_state_enter,
      scif_sas_remote_device_updating_port_width_state_exit
   },
#endif //#if !defined(DISABLE_WIDE_PORTED_TARGETS)
   {
      SCI_BASE_REMOTE_DEVICE_STATE_FINAL,
      scif_sas_remote_device_final_state_enter,
      NULL
   },
};

