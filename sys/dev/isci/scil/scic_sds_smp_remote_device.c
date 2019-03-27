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
 * @brief This file contains This file contains the SMP remote device
 *        object methods and it's state machines.
 */

#include <dev/isci/scil/scic_user_callback.h>
#include <dev/isci/scil/scic_sds_logger.h>
#include <dev/isci/scil/scic_sds_remote_device.h>
#include <dev/isci/scil/scic_sds_controller.h>
#include <dev/isci/scil/scic_sds_port.h>
#include <dev/isci/scil/scic_sds_request.h>
#include <dev/isci/scil/scu_event_codes.h>
#include <dev/isci/scil/scu_task_context.h>
#include <dev/isci/scil/scic_remote_device.h>

//*****************************************************************************
//*  SMP REMOTE DEVICE READY IDLE SUBSTATE HANDLERS
//*****************************************************************************

/**
 * This method will handle the start io operation for a SMP device that is in
 * the idle state.
 *
 * @param [in] device The device the io is sent to.
 * @param [in] request The io to start.
 *
 * @return SCI_STATUS
 */
static
SCI_STATUS scic_sds_smp_remote_device_ready_idle_substate_start_io_handler(
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
         status = scic_sds_request_start(io_request);
      }

      if (status == SCI_SUCCESS)
      {
         this_device->working_request = io_request;

         sci_base_state_machine_change_state(
               &this_device->ready_substate_machine,
               SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_CMD
         );
      }

      scic_sds_remote_device_start_request(this_device, io_request, status);
   }

   return status;
}


//******************************************************************************
//* SMP REMOTE DEVICE READY SUBSTATE CMD HANDLERS
//******************************************************************************
/**
 * This device is already handling a command it can not accept new commands
 * until this one is complete.
 *
 * @param[in] device This is the device object that is receiving the IO.
 *
 * @param[in] request The io to start.
 *
 * @return SCI_STATUS
 */
static
SCI_STATUS scic_sds_smp_remote_device_ready_cmd_substate_start_io_handler(
   SCI_BASE_REMOTE_DEVICE_T * device,
   SCI_BASE_REQUEST_T       * request
)
{
   return SCI_FAILURE_INVALID_STATE;
}


/**
 * @brief this is the complete_io_handler for smp device at ready cmd substate.
 *
 * @param[in] device This is the device object that is receiving the IO.
 * @param[in] request The io to start.
 *
 * @return SCI_STATUS
 */
static
SCI_STATUS scic_sds_smp_remote_device_ready_cmd_substate_complete_io_handler(
   SCI_BASE_REMOTE_DEVICE_T * device,
   SCI_BASE_REQUEST_T       * request
)
{
   SCI_STATUS                 status;
   SCIC_SDS_REMOTE_DEVICE_T * this_device;
   SCIC_SDS_REQUEST_T       * the_request;

   this_device = (SCIC_SDS_REMOTE_DEVICE_T *)device;
   the_request = (SCIC_SDS_REQUEST_T       *)request;

   status = scic_sds_io_request_complete(the_request);

   if (status == SCI_SUCCESS)
   {
      status = scic_sds_port_complete_io(
         this_device->owning_port, this_device, the_request);

      if (status == SCI_SUCCESS)
      {
       scic_sds_remote_device_decrement_request_count(this_device);
         sci_base_state_machine_change_state(
            &this_device->ready_substate_machine,
            SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_IDLE
         );
      }
      else
      {
         SCIC_LOG_ERROR((
            sci_base_object_get_logger(this_device),
            SCIC_LOG_OBJECT_SMP_REMOTE_TARGET,
            "SCIC SDS Remote Device 0x%x io request 0x%x could not be completd on the port 0x%x failed with status %d.\n",
            this_device, the_request, this_device->owning_port, status
         ));
      }
   }

   return status;
}

/**
 * @brief This is frame handler for smp device ready cmd substate.
 *
 * @param[in] this_device This is the device object that is receiving the frame.
 * @param[in] frame_index The index for the frame received.
 *
 * @return SCI_STATUS
 */
static
SCI_STATUS scic_sds_smp_remote_device_ready_cmd_substate_frame_handler(
   SCIC_SDS_REMOTE_DEVICE_T * this_device,
   U32                        frame_index
)
{
   SCI_STATUS status;

   /// The device does not process any UF received from the hardware while
   /// in this state.  All unsolicited frames are forwarded to the io request
   /// object.
   status = scic_sds_io_request_frame_handler(
      this_device->working_request,
      frame_index
   );

   return status;
}

// ---------------------------------------------------------------------------

SCIC_SDS_REMOTE_DEVICE_STATE_HANDLER_T
   scic_sds_smp_remote_device_ready_substate_handler_table[
                              SCIC_SDS_SMP_REMOTE_DEVICE_READY_MAX_SUBSTATES] =
{
   // SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_IDLE
   {
      {
         scic_sds_remote_device_default_start_handler,
         scic_sds_remote_device_ready_state_stop_handler,
         scic_sds_remote_device_default_fail_handler,
         scic_sds_remote_device_default_destruct_handler,
         scic_sds_remote_device_default_reset_handler,
         scic_sds_remote_device_default_reset_complete_handler,
         scic_sds_smp_remote_device_ready_idle_substate_start_io_handler,
         scic_sds_remote_device_default_complete_request_handler,
         scic_sds_remote_device_default_continue_request_handler,
         scic_sds_remote_device_default_start_request_handler,
         scic_sds_remote_device_default_complete_request_handler
      },
      scic_sds_remote_device_default_suspend_handler,
      scic_sds_remote_device_default_resume_handler,
      scic_sds_remote_device_general_event_handler,
      scic_sds_remote_device_default_frame_handler
   },
   // SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_CMD
   {
      {
         scic_sds_remote_device_default_start_handler,
         scic_sds_remote_device_ready_state_stop_handler,
         scic_sds_remote_device_default_fail_handler,
         scic_sds_remote_device_default_destruct_handler,
         scic_sds_remote_device_default_reset_handler,
         scic_sds_remote_device_default_reset_complete_handler,
         scic_sds_smp_remote_device_ready_cmd_substate_start_io_handler,
         scic_sds_smp_remote_device_ready_cmd_substate_complete_io_handler,
         scic_sds_remote_device_default_continue_request_handler,
         scic_sds_remote_device_default_start_request_handler,
         scic_sds_remote_device_default_complete_request_handler
      },
      scic_sds_remote_device_default_suspend_handler,
      scic_sds_remote_device_default_resume_handler,
      scic_sds_remote_device_general_event_handler,
      scic_sds_smp_remote_device_ready_cmd_substate_frame_handler
   }
};

/**
 * This is the SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_IDLE enter method. This
 * method sets the ready cmd substate handlers and reports the device as ready.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast into a
 *       SCIC_SDS_REMOTE_DEVICE.
 *
 * @return none
 */
static
void scic_sds_smp_remote_device_ready_idle_substate_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REMOTE_DEVICE_T *this_device = (SCIC_SDS_REMOTE_DEVICE_T *)object;

   SET_STATE_HANDLER(
      this_device,
      scic_sds_smp_remote_device_ready_substate_handler_table,
      SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_IDLE
   );

   scic_cb_remote_device_ready(
      scic_sds_remote_device_get_controller(this_device), this_device);
}

/**
 * This is the SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_CMD enter method. This
 * method sets the remote device objects ready cmd substate handlers, and notify
 * core user that the device is not ready.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast into a
 *       SCIC_SDS_REMOTE_DEVICE.
 *
 * @return none
 */
static
void scic_sds_smp_remote_device_ready_cmd_substate_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REMOTE_DEVICE_T *this_device = (SCIC_SDS_REMOTE_DEVICE_T *)object;

   ASSERT(this_device->working_request != NULL);

   SET_STATE_HANDLER(
      this_device,
      scic_sds_smp_remote_device_ready_substate_handler_table,
      SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_CMD
   );

   scic_cb_remote_device_not_ready(
      scic_sds_remote_device_get_controller(this_device),
      this_device,
      SCIC_REMOTE_DEVICE_NOT_READY_SMP_REQUEST_STARTED
   );
}

/**
 * This is the SCIC_SDS_SSP_REMOTE_DEVICE_READY_SUBSTATE_CMD exit method.
 *
 * @param[in] object This is the SCI_BASE_OBJECT which is cast into a
 *       SCIC_SDS_REMOTE_DEVICE.
 *
 * @return none
 */
static
void scic_sds_smp_remote_device_ready_cmd_substate_exit(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REMOTE_DEVICE_T * this_device = (SCIC_SDS_REMOTE_DEVICE_T *)object;

   this_device->working_request = NULL;
}

// ---------------------------------------------------------------------------

SCI_BASE_STATE_T
   scic_sds_smp_remote_device_ready_substate_table[
                     SCIC_SDS_SMP_REMOTE_DEVICE_READY_MAX_SUBSTATES] =
{
   {
      SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_IDLE,
      scic_sds_smp_remote_device_ready_idle_substate_enter,
      NULL
   },
   {
      SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_CMD,
      scic_sds_smp_remote_device_ready_cmd_substate_enter,
      scic_sds_smp_remote_device_ready_cmd_substate_exit
   }
};

