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
 * @brief This file contains the task management substate handlers for the
 *        SCIC_SDS_IO_REQUEST object.
 *        This file contains the enter/exit methods associated with each of
 *        the task management raw request states.  For more information on the
 *        task management request state machine please refer to
 *        scic_sds_io_request.h
 */

#include <dev/isci/scil/intel_sas.h>
#include <dev/isci/scil/scic_sds_request.h>
#include <dev/isci/scil/scic_controller.h>
#include <dev/isci/scil/scic_sds_logger.h>
#include <dev/isci/scil/scic_sds_controller.h>
#include <dev/isci/scil/scu_completion_codes.h>
#include <dev/isci/scil/scu_task_context.h>
#include <dev/isci/scil/sci_base_state_machine.h>

/**
 * @brief This method processes the completions transport layer (TL) status
 *        to determine if the RAW task management frame was sent successfully.
 *        If the raw frame was sent successfully, then the state for the
 *        task request transitions to waiting for a response frame.
 *
 * @param[in] this_request This parameter specifies the request for which
 *            the TC completion was received.
 * @param[in] completion_code This parameter indicates the completion status
 *            information for the TC.
 *
 * @return Indicate if the tc completion handler was successful.
 * @retval SCI_SUCCESS currently this method always returns success.
 */
static
SCI_STATUS scic_sds_ssp_task_request_await_tc_completion_tc_completion_handler(
   SCIC_SDS_REQUEST_T * this_request,
   U32                  completion_code
)
{
   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_request),
      SCIC_LOG_OBJECT_TASK_MANAGEMENT,
      "scic_sds_ssp_task_request_await_tc_completion_tc_completion_handler(0x%x, 0x%x) enter\n",
      this_request, completion_code
   ));

   switch (SCU_GET_COMPLETION_TL_STATUS(completion_code))
   {
   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_GOOD):
      scic_sds_request_set_status(
         this_request, SCU_TASK_DONE_GOOD, SCI_SUCCESS
      );

      sci_base_state_machine_change_state(
         &this_request->started_substate_machine,
         SCIC_SDS_IO_REQUEST_STARTED_TASK_MGMT_SUBSTATE_AWAIT_TC_RESPONSE
      );
   break;

   case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_ACK_NAK_TO):
      // Currently, the decision is to simply allow the task request to
      // timeout if the task IU wasn't received successfully.
      // There is a potential for receiving multiple task responses if we
      // decide to send the task IU again.
      SCIC_LOG_WARNING((
         sci_base_object_get_logger(this_request),
         SCIC_LOG_OBJECT_TASK_MANAGEMENT,
         "TaskRequest:0x%x CompletionCode:%x - ACK/NAK timeout\n",
         this_request, completion_code
      ));

      sci_base_state_machine_change_state(
         &this_request->started_substate_machine,
         SCIC_SDS_IO_REQUEST_STARTED_TASK_MGMT_SUBSTATE_AWAIT_TC_RESPONSE
      );
   break;

   default:
      // All other completion status cause the IO to be complete.  If a NAK
      // was received, then it is up to the user to retry the request.
      scic_sds_request_set_status(
         this_request,
         SCU_NORMALIZE_COMPLETION_STATUS(completion_code),
         SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR
      );

      sci_base_state_machine_change_state(
         &this_request->parent.state_machine,
         SCI_BASE_REQUEST_STATE_COMPLETED
      );
   break;
   }

   return SCI_SUCCESS;
}

/**
 * @brief This method is responsible for processing a terminate/abort
 *        request for this TC while the request is waiting for the task
 *        management response unsolicited frame.
 *
 * @param[in] this_request This parameter specifies the request for which
 *            the termination was requested.
 *
 * @return This method returns an indication as to whether the abort
 *         request was successfully handled.
 *
 * @todo need to update to ensure the received UF doesn't cause damage
 *       to subsequent requests (i.e. put the extended tag in a holding
 *       pattern for this particular device).
 */
static
SCI_STATUS scic_sds_ssp_task_request_await_tc_response_abort_handler(
   SCI_BASE_REQUEST_T * request
)
{
   SCIC_SDS_REQUEST_T *this_request = (SCIC_SDS_REQUEST_T *)request;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_request),
      SCIC_LOG_OBJECT_TASK_MANAGEMENT,
      "scic_sds_ssp_task_request_await_tc_response_abort_handler(0x%x) enter\n",
      this_request
   ));

   sci_base_state_machine_change_state(
      &this_request->parent.state_machine,
      SCI_BASE_REQUEST_STATE_ABORTING
   );

   sci_base_state_machine_change_state(
      &this_request->parent.state_machine,
      SCI_BASE_REQUEST_STATE_COMPLETED
   );

   return SCI_SUCCESS;
}

/**
 * @brief This method processes an unsolicited frame while the task mgmt
 *        request is waiting for a response frame.  It will copy the
 *        response data, release the unsolicited frame, and transition
 *        the request to the SCI_BASE_REQUEST_STATE_COMPLETED state.
 *
 * @param[in] this_request This parameter specifies the request for which
 *            the unsolicited frame was received.
 * @param[in] frame_index This parameter indicates the unsolicited frame
 *            index that should contain the response.
 *
 * @return This method returns an indication of whether the TC response
 *         frame was handled successfully or not.
 * @retval SCI_SUCCESS Currently this value is always returned and indicates
 *         successful processing of the TC response.
 *
 * @todo Should probably update to check frame type and make sure it is
 *       a response frame.
 */
static
SCI_STATUS scic_sds_ssp_task_request_await_tc_response_frame_handler(
   SCIC_SDS_REQUEST_T * this_request,
   U32                  frame_index
)
{
   // Save off the controller, so that we do not touch the request after it
   //  is completed.
   SCIC_SDS_CONTROLLER_T * owning_controller = this_request->owning_controller;

   SCIC_LOG_TRACE((
      sci_base_object_get_logger(this_request),
      SCIC_LOG_OBJECT_TASK_MANAGEMENT,
      "scic_sds_ssp_task_request_await_tc_response_frame_handler(0x%x, 0x%x) enter\n",
      this_request, frame_index
   ));

   scic_sds_io_request_copy_response(this_request);

   sci_base_state_machine_change_state(
      &this_request->parent.state_machine,
      SCI_BASE_REQUEST_STATE_COMPLETED
   );

   scic_sds_controller_release_frame(
      owning_controller, frame_index
   );

   return SCI_SUCCESS;
}

SCIC_SDS_IO_REQUEST_STATE_HANDLER_T
scic_sds_ssp_task_request_started_substate_handler_table
[SCIC_SDS_IO_REQUEST_STARTED_TASK_MGMT_MAX_SUBSTATES] =
{
   // SCIC_SDS_IO_REQUEST_STARTED_TASK_MGMT_SUBSTATE_AWAIT_TC_COMPLETION
   {
      {
         scic_sds_request_default_start_handler,
         scic_sds_request_started_state_abort_handler,
         scic_sds_request_default_complete_handler,
         scic_sds_request_default_destruct_handler
      },
      scic_sds_ssp_task_request_await_tc_completion_tc_completion_handler,
      scic_sds_request_default_event_handler,
      scic_sds_request_default_frame_handler
   },
   // SCIC_SDS_IO_REQUEST_STARTED_TASK_MGMT_SUBSTATE_AWAIT_TC_RESPONSE
   {
      {
         scic_sds_request_default_start_handler,
         scic_sds_ssp_task_request_await_tc_response_abort_handler,
         scic_sds_request_default_complete_handler,
         scic_sds_request_default_destruct_handler
      },
      scic_sds_request_default_tc_completion_handler,
      scic_sds_request_default_event_handler,
      scic_sds_ssp_task_request_await_tc_response_frame_handler
   }
};

/**
 * @brief This method performs the actions required when entering the
 *        SCIC_SDS_IO_REQUEST_STARTED_TASK_MGMT_SUBSTATE_AWAIT_TC_COMPLETION
 *        sub-state.  This includes setting the IO request state handlers
 *        for this sub-state.
 *
 * @param[in]  object This parameter specifies the request object for which
 *             the sub-state change is occurring.
 *
 * @return none.
 */
static
void scic_sds_io_request_started_task_mgmt_await_tc_completion_substate_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REQUEST_T *this_request = (SCIC_SDS_REQUEST_T *)object;

   SET_STATE_HANDLER(
      this_request,
      scic_sds_ssp_task_request_started_substate_handler_table,
      SCIC_SDS_IO_REQUEST_STARTED_TASK_MGMT_SUBSTATE_AWAIT_TC_COMPLETION
   );
}

/**
 * @brief This method performs the actions required when entering the
 *        SCIC_SDS_IO_REQUEST_STARTED_SUBSTATE_AWAIT_TC_RESPONSE sub-state.
 *        This includes setting the IO request state handlers for this
 *        sub-state.
 *
 * @param[in]  object This parameter specifies the request object for which
 *             the sub-state change is occurring.
 *
 * @return none.
 */
static
void scic_sds_io_request_started_task_mgmt_await_task_response_substate_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIC_SDS_REQUEST_T *this_request = (SCIC_SDS_REQUEST_T *)object;

   SET_STATE_HANDLER(
      this_request,
      scic_sds_ssp_task_request_started_substate_handler_table,
      SCIC_SDS_IO_REQUEST_STARTED_TASK_MGMT_SUBSTATE_AWAIT_TC_RESPONSE
   );
}

SCI_BASE_STATE_T scic_sds_io_request_started_task_mgmt_substate_table
[SCIC_SDS_IO_REQUEST_STARTED_TASK_MGMT_MAX_SUBSTATES] =
{
   {
      SCIC_SDS_IO_REQUEST_STARTED_TASK_MGMT_SUBSTATE_AWAIT_TC_COMPLETION,
      scic_sds_io_request_started_task_mgmt_await_tc_completion_substate_enter,
      NULL
   },
   {
      SCIC_SDS_IO_REQUEST_STARTED_TASK_MGMT_SUBSTATE_AWAIT_TC_RESPONSE,
      scic_sds_io_request_started_task_mgmt_await_task_response_substate_enter,
      NULL
   }
};


