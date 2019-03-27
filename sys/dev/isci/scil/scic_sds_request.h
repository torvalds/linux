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
 *
 * $FreeBSD$
 */
#ifndef _SCIC_SDS_IO_REQUEST_H_
#define _SCIC_SDS_IO_REQUEST_H_

/**
 * @file
 *
 * @brief This file contains the structures, constants and prototypes for the
 *        SCIC_SDS_IO_REQUEST object.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <sys/param.h>

#include <dev/isci/scil/scic_io_request.h>

#include <dev/isci/scil/sci_base_request.h>
#include <dev/isci/scil/sci_base_state_machine_logger.h>
#include <dev/isci/scil/scu_task_context.h>
#include <dev/isci/scil/intel_sas.h>

struct SCIC_SDS_CONTROLLER;
struct SCIC_SDS_REMOTE_DEVICE;
struct SCIC_SDS_IO_REQUEST_STATE_HANDLER;

/**
 * @enum _SCIC_SDS_IO_REQUEST_STARTED_TASK_MGMT_SUBSTATES
 *
 * @brief This enumeration depicts all of the substates for a task
 *        management request to be performed in the STARTED super-state.
 */
typedef enum _SCIC_SDS_RAW_REQUEST_STARTED_TASK_MGMT_SUBSTATES
{
   /**
    * The AWAIT_TC_COMPLETION sub-state indicates that the started raw
    * task management request is waiting for the transmission of the
    * initial frame (i.e. command, task, etc.).
    */
   SCIC_SDS_IO_REQUEST_STARTED_TASK_MGMT_SUBSTATE_AWAIT_TC_COMPLETION,

   /**
    * This sub-state indicates that the started task management request
    * is waiting for the reception of an unsolicited frame
    * (i.e. response IU).
    */
   SCIC_SDS_IO_REQUEST_STARTED_TASK_MGMT_SUBSTATE_AWAIT_TC_RESPONSE,

   SCIC_SDS_IO_REQUEST_STARTED_TASK_MGMT_MAX_SUBSTATES

} SCIC_SDS_RAW_REQUEST_STARTED_TASK_MGMT_SUBSTATES;


/**
 * @enum _SCIC_SDS_SMP_REQUEST_STARTED_SUBSTATES
 *
 * @brief This enumeration depicts all of the substates for a SMP
 *        request to be performed in the STARTED super-state.
 */
typedef enum _SCIC_SDS_SMP_REQUEST_STARTED_SUBSTATES
{
   /**
    * This sub-state indicates that the started task management request
    * is waiting for the reception of an unsolicited frame
    * (i.e. response IU).
    */
   SCIC_SDS_SMP_REQUEST_STARTED_SUBSTATE_AWAIT_RESPONSE,

   /**
    * The AWAIT_TC_COMPLETION sub-state indicates that the started SMP request is
    * waiting for the transmission of the initial frame (i.e. command, task, etc.).
    */
   SCIC_SDS_SMP_REQUEST_STARTED_SUBSTATE_AWAIT_TC_COMPLETION,

   SCIC_SDS_SMP_REQUEST_STARTED_MAX_SUBSTATES

} SCIC_SDS_SMP_REQUEST_STARTED_SUBSTATES;

/**
 * @struct SCIC_SDS_IO_REQUEST
 *
 * @brief This structure contains or references all of the data necessary
 *        to process a task management or normal IO request.
 */
typedef struct SCIC_SDS_REQUEST
{
   /**
    * This field indictes the parent object of the request.
    */
   SCI_BASE_REQUEST_T parent;

   void *user_request;

   /**
    * This field simply points to the controller to which this IO request
    * is associated.
    */
   struct SCIC_SDS_CONTROLLER    *owning_controller;

   /**
    * This field simply points to the remote device to which this IO request
    * is associated.
    */
   struct SCIC_SDS_REMOTE_DEVICE *target_device;

   /**
    * This field is utilized to determine if the SCI user is managing
    * the IO tag for this request or if the core is managing it.
    */
   BOOL was_tag_assigned_by_user;

   /**
    * This field indicates the IO tag for this request.  The IO tag is
    * comprised of the task_index and a sequence count. The sequence count
    * is utilized to help identify tasks from one life to another.
    */
   U16 io_tag;

   /**
   * This field specifies the sat protocol being utilized for this
   * IO request, such as SAT_PROTOCOL_PIO_DATA_IN, SAT_PROTOCOL_FPDMA etc.
   */
   U8 sat_protocol;

   /**
    * This field specifies the protocol being utilized for this
    * IO request.
    */
   SCIC_TRANSPORT_PROTOCOL protocol;

   /**
    * This field indicates the completion status taken from the SCUs
    * completion code.  It indicates the completion result for the SCU hardware.
    */
   U32 scu_status;

   /**
    * This field indicates the completion status returned to the SCI user.  It
    * indicates the users view of the io request completion.
    */
   U32 sci_status;

   /**
    * This field contains the value to be utilized when posting (e.g. Post_TC,
    * Post_TC_Abort) this request to the silicon.
    */
   U32 post_context;

   void                   *command_buffer;
   void                   *response_buffer;
   SCU_TASK_CONTEXT_T     *task_context_buffer;
   SCU_SGL_ELEMENT_PAIR_T *sgl_element_pair_buffer;

   /**
    * This field indicates if this request is a task management request or
    * normal IO request.
    */
   BOOL is_task_management_request;

   /**
    * This field indicates that this request contains an initialized started
    * substate machine.
    */
   BOOL has_started_substate_machine;

   /**
    * This field is a pointer to the stored rx frame data.  It is used in STP
    * internal requests and SMP response frames.  If this field is non-NULL the
    * saved frame must be released on IO request completion.
    *
    * @todo In the future do we want to keep a list of RX frame buffers?
    */
   U32 saved_rx_frame_index;

   /**
    * This field specifies the data necessary to manage the sub-state
    * machine executed while in the SCI_BASE_REQUEST_STATE_STARTED state.
    */
   SCI_BASE_STATE_MACHINE_T started_substate_machine;

   /**
    * This field specifies the current state handlers in place for this
    * IO Request object.  This field is updated each time the request
    * changes state.
    */
   struct SCIC_SDS_IO_REQUEST_STATE_HANDLER *state_handlers;

   #ifdef SCI_LOGGING
   /**
    * This field is the observer of the started subsate machine
    */
   SCI_BASE_STATE_MACHINE_LOGGER_T started_substate_machine_logger;
   #endif

   /**
    * This field in the recorded device sequence for the io request.  This is
    * recorded during the build operation and is compared in the start
    * operation.  If the sequence is different then there was a change of
    * devices from the build to start operations.
    */
   U8  device_sequence;

} SCIC_SDS_REQUEST_T;


typedef SCI_STATUS (*SCIC_SDS_IO_REQUEST_FRAME_HANDLER_T)(
                         SCIC_SDS_REQUEST_T * this_request,
                         U32                  frame_index);

typedef SCI_STATUS (*SCIC_SDS_IO_REQUEST_EVENT_HANDLER_T)(
                         SCIC_SDS_REQUEST_T * this_request,
                         U32                  event_code);

typedef SCI_STATUS (*SCIC_SDS_IO_REQUEST_TASK_COMPLETION_HANDLER_T)(
                         SCIC_SDS_REQUEST_T * this_request,
                         U32                  completion_code);

/**
 * @struct SCIC_SDS_IO_REQUEST_STATE_HANDLER
 *
 * @brief This is the SDS core definition of the state handlers.
 */
typedef struct SCIC_SDS_IO_REQUEST_STATE_HANDLER
{
   SCI_BASE_REQUEST_STATE_HANDLER_T parent;

   SCIC_SDS_IO_REQUEST_TASK_COMPLETION_HANDLER_T  tc_completion_handler;
   SCIC_SDS_IO_REQUEST_EVENT_HANDLER_T            event_handler;
   SCIC_SDS_IO_REQUEST_FRAME_HANDLER_T            frame_handler;

} SCIC_SDS_IO_REQUEST_STATE_HANDLER_T;

extern SCI_BASE_STATE_T scic_sds_request_state_table[];
extern SCIC_SDS_IO_REQUEST_STATE_HANDLER_T
       scic_sds_request_state_handler_table[];

extern SCI_BASE_STATE_T scic_sds_io_request_started_task_mgmt_substate_table[];
extern SCIC_SDS_IO_REQUEST_STATE_HANDLER_T
       scic_sds_ssp_task_request_started_substate_handler_table[];

extern SCI_BASE_STATE_T scic_sds_smp_request_started_substate_table[];
extern SCIC_SDS_IO_REQUEST_STATE_HANDLER_T
       scic_sds_smp_request_started_substate_handler_table[];

/**
 * This macro returns the maximum number of SGL element paris that we will
 * support in a single IO request.
 */
#define SCU_MAX_SGL_ELEMENT_PAIRS ((SCU_IO_REQUEST_SGE_COUNT + 1) / 2)

/**
 * This macro will return the controller for this io request object
 */
#define scic_sds_request_get_controller(this_request) \
   ((this_request)->owning_controller)

/**
 * This macro will return the device for this io request object
 */
#define scic_sds_request_get_device(this_request) \
   ((this_request)->target_device)

/**
 * This macro will return the port for this io request object
 */
#define scic_sds_request_get_port(this_request) \
   scic_sds_remote_device_get_port(scic_sds_request_get_device(this_request))

/**
 * This macro returns the constructed post context result for the io
 * request.
 */
#define scic_sds_request_get_post_context(this_request) \
   ((this_request)->post_context)

/**
 * This is a helper macro to return the os handle for this request object.
 */
#define scic_sds_request_get_task_context(request) \
   ((request)->task_context_buffer)

#define scic_sds_request_align_task_context_buffer(address) \
   ((SCU_TASK_CONTEXT_T *)( \
       (((POINTER_UINT)(address)) + (CACHE_LINE_SIZE - 1)) \
     & ~(CACHE_LINE_SIZE - 1) \
   ))

/**
 * This macro will align the memory address so that it is correct for the SCU
 * hardware to DMA the SGL element pairs.
 */
#define scic_sds_request_align_sgl_element_buffer(address) \
   ((SCU_SGL_ELEMENT_PAIR_T *)( \
     ((char *)(address)) \
   + ( \
         ((~(POINTER_UINT)(address)) + 1) \
       & (sizeof(SCU_SGL_ELEMENT_PAIR_T) - 1) \
     ) \
   ))

/**
 * This macro will set the scu hardware status and sci request completion
 * status for an io request.
 */
#define scic_sds_request_set_status(request, scu_status_code, sci_status_code) \
{ \
   (request)->scu_status = (scu_status_code); \
   (request)->sci_status = (sci_status_code); \
}

#define scic_sds_request_complete(a_request) \
   ((a_request)->state_handlers->parent.complete_handler(&(a_request)->parent))

U32 scic_sds_request_get_min_timer_count(void);

U32 scic_sds_request_get_max_timer_count(void);


/**
 * This macro invokes the core state task completion handler for the
 * SCIC_SDS_IO_REQUEST_T object.
 */
#define scic_sds_io_request_tc_completion(this_request, completion_code) \
{ \
   if (this_request->parent.state_machine.current_state_id  \
          == SCI_BASE_REQUEST_STATE_STARTED \
       && this_request->has_started_substate_machine \
          == FALSE) \
      scic_sds_request_started_state_tc_completion_handler(this_request, completion_code); \
   else \
      this_request->state_handlers->tc_completion_handler(this_request, completion_code); \
}

/**
 * This macro zeros the hardware SGL element data
 */
#define SCU_SGL_ZERO(scu_sge) \
{ \
   (scu_sge).length = 0; \
   (scu_sge).address_lower = 0; \
   (scu_sge).address_upper = 0; \
   (scu_sge).address_modifier = 0; \
}

/**
 * This macro copys the SGL Element data from the host os to the hardware SGL
 * elment data
 */
#define SCU_SGL_COPY(os_handle, scu_sge, os_sge) \
{ \
   (scu_sge).length = \
      scic_cb_sge_get_length_field(os_handle, os_sge); \
   (scu_sge).address_upper = \
      sci_cb_physical_address_upper(scic_cb_sge_get_address_field(os_handle, os_sge)); \
   (scu_sge).address_lower = \
      sci_cb_physical_address_lower(scic_cb_sge_get_address_field(os_handle, os_sge)); \
   (scu_sge).address_modifier = 0; \
}

//*****************************************************************************
//* CORE REQUEST PROTOTYPES
//*****************************************************************************

SCU_SGL_ELEMENT_PAIR_T *scic_sds_request_get_sgl_element_pair(
   SCIC_SDS_REQUEST_T *this_request,
   U32                 sgl_pair_index
);

void scic_sds_request_build_sgl(
   SCIC_SDS_REQUEST_T *this_request
);

void scic_sds_ssp_io_request_assign_buffers(
   SCIC_SDS_REQUEST_T *this_request
);

void scic_sds_ssp_task_request_assign_buffers(
   SCIC_SDS_REQUEST_T *this_request
);

void scic_sds_stp_request_assign_buffers(
   SCIC_SDS_REQUEST_T * this_request
);

void scic_sds_smp_request_assign_buffers(
   SCIC_SDS_REQUEST_T * this_request
);

// ---------------------------------------------------------------------------

SCI_STATUS scic_sds_request_start(
   SCIC_SDS_REQUEST_T *this_request
);

SCI_STATUS scic_sds_io_request_terminate(
   SCIC_SDS_REQUEST_T *this_request
);

SCI_STATUS scic_sds_io_request_complete(
   SCIC_SDS_REQUEST_T *this_request
);

void scic_sds_io_request_copy_response(
   SCIC_SDS_REQUEST_T *this_request
);

SCI_STATUS scic_sds_io_request_event_handler(
   SCIC_SDS_REQUEST_T *this_request,
   U32                    event_code
);

SCI_STATUS scic_sds_io_request_frame_handler(
   SCIC_SDS_REQUEST_T *this_request,
   U32                    frame_index
);

SCI_STATUS scic_sds_task_request_complete(
   SCIC_SDS_REQUEST_T *this_request
);

SCI_STATUS scic_sds_task_request_terminate(
   SCIC_SDS_REQUEST_T *this_request
);

#ifdef SCI_LOGGING
void scic_sds_request_initialize_state_logging(
   SCIC_SDS_REQUEST_T *this_request
);

void scic_sds_request_deinitialize_state_logging(
   SCIC_SDS_REQUEST_T *this_request
);
#else // SCI_LOGGING
#define scic_sds_request_initialize_state_logging(x)
#define scic_sds_request_deinitialize_state_logging(x)
#endif // SCI_LOGGING

//*****************************************************************************
//* DEFAULT STATE HANDLERS
//*****************************************************************************

SCI_STATUS scic_sds_request_default_start_handler(
   SCI_BASE_REQUEST_T *this_request
);

SCI_STATUS scic_sds_request_default_abort_handler(
   SCI_BASE_REQUEST_T *this_request
);

SCI_STATUS scic_sds_request_default_complete_handler(
   SCI_BASE_REQUEST_T *this_request
);

SCI_STATUS scic_sds_request_default_destruct_handler(
   SCI_BASE_REQUEST_T *this_request
);

SCI_STATUS scic_sds_request_default_tc_completion_handler(
   SCIC_SDS_REQUEST_T * this_request,
   U32                  completion_code
);

SCI_STATUS scic_sds_request_default_event_handler(
   SCIC_SDS_REQUEST_T * this_request,
   U32                  event_code
);

SCI_STATUS scic_sds_request_default_frame_handler(
   SCIC_SDS_REQUEST_T * this_request,
   U32                  frame_index
);

//*****************************************************************************
//* STARTED STATE HANDLERS
//*****************************************************************************

SCI_STATUS scic_sds_request_started_state_abort_handler(
   SCI_BASE_REQUEST_T *this_request
);

SCI_STATUS scic_sds_request_started_state_tc_completion_handler(
   SCIC_SDS_REQUEST_T * this_request,
   U32                  completion_code
);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIC_SDS_IO_REQUEST_H_
