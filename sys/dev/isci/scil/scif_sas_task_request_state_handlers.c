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
 *        to the framework io request state handler methods.
 */

#include <dev/isci/scil/scif_sas_logger.h>
#include <dev/isci/scil/scif_sas_task_request.h>

//******************************************************************************
//* C O N S T R U C T E D   H A N D L E R S
//******************************************************************************

/**
 * @brief This method provides CONSTRUCTED state specific handling for
 *        when the user attempts to start the supplied task request.
 *
 * @param[in] task_request This parameter specifies the task request object
 *            to be started.
 *
 * @return This method returns a value indicating if the task request was
 *         successfully started or not.
 * @retval SCI_SUCCESS This return value indicates successful starting
 *         of the task request.
 */
static
SCI_STATUS scif_sas_task_request_constructed_start_handler(
   SCI_BASE_REQUEST_T * task_request
)
{
   sci_base_state_machine_change_state(
      &task_request->state_machine, SCI_BASE_REQUEST_STATE_STARTED
   );

   return SCI_SUCCESS;
}

/**
 * @brief This method provides CONSTRUCTED state specific handling for
 *        when the user attempts to abort the supplied task request.
 *
 * @param[in] task_request This parameter specifies the task request object
 *            to be aborted.
 *
 * @return This method returns a value indicating if the task request was
 *         successfully aborted or not.
 * @retval SCI_SUCCESS This return value indicates successful aborting
 *         of the task request.
 */
static
SCI_STATUS scif_sas_task_request_constructed_abort_handler(
   SCI_BASE_REQUEST_T * task_request
)
{
   sci_base_state_machine_change_state(
      &task_request->state_machine, SCI_BASE_REQUEST_STATE_COMPLETED
   );

   return SCI_SUCCESS;
}

//******************************************************************************
//* S T A R T E D   H A N D L E R S
//******************************************************************************

/**
 * @brief This method provides STARTED state specific handling for
 *        when the user attempts to abort the supplied task request.
 *
 * @param[in] task_request This parameter specifies the task request object
 *            to be aborted.
 *
 * @return This method returns a value indicating if the aborting the
 *         task request was successfully started.
 * @retval SCI_SUCCESS This return value indicates that the abort process
 *         began successfully.
 */
static
SCI_STATUS scif_sas_task_request_started_abort_handler(
   SCI_BASE_REQUEST_T * task_request
)
{
   SCIF_SAS_REQUEST_T * fw_request = (SCIF_SAS_REQUEST_T *) task_request;

   sci_base_state_machine_change_state(
      &task_request->state_machine, SCI_BASE_REQUEST_STATE_ABORTING
   );

   return fw_request->status;
}

/**
 * @brief This method provides STARTED state specific handling for
 *        when the user attempts to complete the supplied task request.
 *
 * @param[in] task_request This parameter specifies the task request object
 *            to be completed.
 *
 * @return This method returns a value indicating if the completion of the
 *         task request was successful.
 * @retval SCI_SUCCESS This return value indicates that the completion process
 *         was successful.
 */
static
SCI_STATUS scif_sas_task_request_started_complete_handler(
   SCI_BASE_REQUEST_T * task_request
)
{
   sci_base_state_machine_change_state(
      &task_request->state_machine, SCI_BASE_REQUEST_STATE_COMPLETED
   );

   return SCI_SUCCESS;
}

//******************************************************************************
//* C O M P L E T E D   H A N D L E R S
//******************************************************************************

/**
 * @brief This method provides COMPLETED state specific handling for
 *        when the user attempts to destruct the supplied task request.
 *
 * @param[in] task_request This parameter specifies the task request object
 *            to be destructed.
 *
 * @return This method returns a value indicating if the destruct
 *         operation was successful.
 * @retval SCI_SUCCESS This return value indicates that the destruct
 *         was successful.
 */
static
SCI_STATUS scif_sas_task_request_completed_destruct_handler(
   SCI_BASE_REQUEST_T * task_request
)
{
   SCIF_SAS_REQUEST_T * fw_request = (SCIF_SAS_REQUEST_T *)task_request;

   sci_base_state_machine_change_state(
      &task_request->state_machine, SCI_BASE_REQUEST_STATE_FINAL
   );

   sci_base_state_machine_logger_deinitialize(
      &task_request->state_machine_logger,
      &task_request->state_machine
   );

   if (fw_request->is_internal == TRUE)
   {
      scif_sas_internal_task_request_destruct(
         (SCIF_SAS_TASK_REQUEST_T *)fw_request
      );
   }

   return SCI_SUCCESS;
}

//******************************************************************************
//* A B O R T I N G   H A N D L E R S
//******************************************************************************

/**
 * @brief This method provides ABORTING state specific handling for
 *        when the user attempts to complete the supplied task request.
 *
 * @param[in] task_request This parameter specifies the task request object
 *            to be completed.
 *
 * @return This method returns a value indicating if the completion
 *         operation was successful.
 * @retval SCI_SUCCESS This return value indicates that the completion
 *         was successful.
 */
static
SCI_STATUS scif_sas_task_request_aborting_complete_handler(
   SCI_BASE_REQUEST_T * task_request
)
{
   sci_base_state_machine_change_state(
      &task_request->state_machine, SCI_BASE_REQUEST_STATE_COMPLETED
   );

   return SCI_SUCCESS;
}

//******************************************************************************
//* D E F A U L T   H A N D L E R S
//******************************************************************************

/**
 * @brief This method provides DEFAULT handling for when the user
 *        attempts to start the supplied task request.
 *
 * @param[in] task_request This parameter specifies the task request object
 *            to be started.
 *
 * @return This method returns an indication that the start operation is
 *         not allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
static
SCI_STATUS scif_sas_task_request_default_start_handler(
   SCI_BASE_REQUEST_T * task_request
)
{
   SCIF_LOG_ERROR((
      sci_base_object_get_logger((SCIF_SAS_TASK_REQUEST_T *) task_request),
      SCIF_LOG_OBJECT_TASK_MANAGEMENT,
      "TaskRequest:0x%x State:0x%x invalid state to start\n",
      task_request,
      sci_base_state_machine_get_state(
         &((SCIF_SAS_TASK_REQUEST_T *) task_request)->parent.parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * @brief This method provides DEFAULT handling for when the user
 *        attempts to abort the supplied task request.
 *
 * @param[in] task_request This parameter specifies the task request object
 *            to be aborted.
 *
 * @return This method returns an indication that the abort operation is
 *         not allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
static
SCI_STATUS scif_sas_task_request_default_abort_handler(
   SCI_BASE_REQUEST_T * task_request
)
{
   SCIF_LOG_ERROR((
      sci_base_object_get_logger((SCIF_SAS_TASK_REQUEST_T *) task_request),
      SCIF_LOG_OBJECT_TASK_MANAGEMENT,
      "TaskRequest:0x%x State:0x%x invalid state to abort\n",
      task_request,
      sci_base_state_machine_get_state(
         &((SCIF_SAS_TASK_REQUEST_T *) task_request)->parent.parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * @brief This method provides DEFAULT handling for when the user
 *        attempts to complete the supplied task request.
 *
 * @param[in] task_request This parameter specifies the task request object
 *            to be completed.
 *
 * @return This method returns an indication that complete operation is
 *         not allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
static
SCI_STATUS scif_sas_task_request_default_complete_handler(
   SCI_BASE_REQUEST_T * task_request
)
{
   SCIF_LOG_ERROR((
      sci_base_object_get_logger((SCIF_SAS_TASK_REQUEST_T *) task_request),
      SCIF_LOG_OBJECT_TASK_MANAGEMENT,
      "TaskRequest:0x%x State:0x%x invalid state to complete\n",
      task_request,
      sci_base_state_machine_get_state(
         &((SCIF_SAS_TASK_REQUEST_T *) task_request)->parent.parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * @brief This method provides DEFAULT handling for when the user
 *        attempts to destruct the supplied task request.
 *
 * @param[in] task_request This parameter specifies the task request object
 *            to be destructed.
 *
 * @return This method returns an indication that destruct operation is
 *         not allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
static
SCI_STATUS scif_sas_task_request_default_destruct_handler(
   SCI_BASE_REQUEST_T * task_request
)
{
   SCIF_LOG_ERROR((
      sci_base_object_get_logger((SCIF_SAS_TASK_REQUEST_T *) task_request),
      SCIF_LOG_OBJECT_TASK_MANAGEMENT,
      "TaskRequest:0x%x State:0x%x invalid state to destruct.\n",
      task_request,
      sci_base_state_machine_get_state(
         &((SCIF_SAS_TASK_REQUEST_T *) task_request)->parent.parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}


SCI_BASE_REQUEST_STATE_HANDLER_T scif_sas_task_request_state_handler_table[] =
{
   // SCI_BASE_REQUEST_STATE_INITIAL
   {
      scif_sas_task_request_default_start_handler,
      scif_sas_task_request_default_abort_handler,
      scif_sas_task_request_default_complete_handler,
      scif_sas_task_request_default_destruct_handler
   },
   // SCI_BASE_REQUEST_STATE_CONSTRUCTED
   {
      scif_sas_task_request_constructed_start_handler,
      scif_sas_task_request_constructed_abort_handler,
      scif_sas_task_request_default_complete_handler,
      scif_sas_task_request_default_destruct_handler
   },
   // SCI_BASE_REQUEST_STATE_STARTED
   {
      scif_sas_task_request_default_start_handler,
      scif_sas_task_request_started_abort_handler,
      scif_sas_task_request_started_complete_handler,
      scif_sas_task_request_default_destruct_handler
   },
   // SCI_BASE_REQUEST_STATE_COMPLETED
   {
      scif_sas_task_request_default_start_handler,
      scif_sas_task_request_default_abort_handler,
      scif_sas_task_request_default_complete_handler,
      scif_sas_task_request_completed_destruct_handler
   },
   // SCI_BASE_REQUEST_STATE_ABORTING
   {
      scif_sas_task_request_default_start_handler,
      scif_sas_task_request_default_abort_handler,
      scif_sas_task_request_aborting_complete_handler,
      scif_sas_task_request_default_destruct_handler
   },
   // SCI_BASE_REQUEST_STATE_FINAL
   {
      scif_sas_task_request_default_start_handler,
      scif_sas_task_request_default_abort_handler,
      scif_sas_task_request_default_complete_handler,
      scif_sas_task_request_default_destruct_handler
   },
};

