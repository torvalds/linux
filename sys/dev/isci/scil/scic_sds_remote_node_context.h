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
#ifndef _SCIC_SDS_REMOTE_NODE_CONTEXT_H_
#define _SCIC_SDS_REMOTE_NODE_CONTEXT_H_

/**
 * @file
 *
 * @brief This file contains the structures, constants, and prototypes
 *        associated with the remote node context in the silicon.  It
 *        exists to model and manage the remote node context in the silicon.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_types.h>
#include <dev/isci/scil/sci_base_state.h>
#include <dev/isci/scil/sci_base_state_machine.h>
#include <dev/isci/scil/sci_base_state_machine_logger.h>

// ---------------------------------------------------------------------------

/**
 * This constant represents an invalid remote device id, it is used to program
 * the STPDARNI register so the driver knows when it has received a SIGNATURE
 * FIS from the SCU.
 */
#define SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX    0x0FFF

#define SCU_HARDWARE_SUSPENSION  (0)
#define SCI_SOFTWARE_SUSPENSION  (1)

struct SCIC_SDS_REQUEST;
struct SCIC_SDS_REMOTE_DEVICE;
struct SCIC_SDS_REMOTE_NODE_CONTEXT;

typedef void (*SCIC_SDS_REMOTE_NODE_CONTEXT_CALLBACK)(void *);

typedef SCI_STATUS (*SCIC_SDS_REMOTE_NODE_CONTEXT_OPERATION)(
   struct SCIC_SDS_REMOTE_NODE_CONTEXT    * this_rnc,
   SCIC_SDS_REMOTE_NODE_CONTEXT_CALLBACK   the_callback,
   void                                   * callback_parameter
);

typedef SCI_STATUS (*SCIC_SDS_REMOTE_NODE_CONTEXT_SUSPEND_OPERATION)(
   struct SCIC_SDS_REMOTE_NODE_CONTEXT    * this_rnc,
   U32                                      suspension_type,
   SCIC_SDS_REMOTE_NODE_CONTEXT_CALLBACK   the_callback,
   void                                   * callback_parameter
);

typedef SCI_STATUS (* SCIC_SDS_REMOTE_NODE_CONTEXT_IO_REQUEST)(
   struct SCIC_SDS_REMOTE_NODE_CONTEXT * this_rnc,
   struct SCIC_SDS_REQUEST             * the_request
);

typedef SCI_STATUS (*SCIC_SDS_REMOTE_NODE_CONTEXT_EVENT_HANDLER)(
   struct SCIC_SDS_REMOTE_NODE_CONTEXT * this_rnc,
   U32                                   event_code
);

// ---------------------------------------------------------------------------

typedef struct _SCIC_SDS_REMOTE_NODE_CONTEXT_HANDLERS
{
   /**
    * This handle is invoked to stop the RNC.  The callback is invoked when after
    * the hardware notification that the RNC has been invalidated.
    */
   SCIC_SDS_REMOTE_NODE_CONTEXT_OPERATION destruct_handler;

   /**
    * This handler is invoked when there is a request to suspend  the RNC.  The
    * callback is invoked after the hardware notification that the remote node is
    * suspended.
    */
   SCIC_SDS_REMOTE_NODE_CONTEXT_SUSPEND_OPERATION suspend_handler;

   /**
    * This handler is invoked when there is a request to resume the RNC.  The
    * callback is invoked when after the RNC has reached the ready state.
    */
   SCIC_SDS_REMOTE_NODE_CONTEXT_OPERATION resume_handler;

   /**
    * This handler is invoked when there is a request to start an io request
    * operation.
    */
   SCIC_SDS_REMOTE_NODE_CONTEXT_IO_REQUEST start_io_handler;

   /**
    * This handler is invoked when there is a request to start a task request
    * operation.
    */
   SCIC_SDS_REMOTE_NODE_CONTEXT_IO_REQUEST start_task_handler;

   /**
    * This handler is invoked where there is an RNC event that must be processed.
    */
   SCIC_SDS_REMOTE_NODE_CONTEXT_EVENT_HANDLER event_handler;

} SCIC_SDS_REMOTE_NODE_CONTEXT_HANDLERS;

// ---------------------------------------------------------------------------

/**
 * @enum
 *
 * This is the enumeration of the remote node context states.
 */
typedef enum _SCIS_SDS_REMOTE_NODE_CONTEXT_STATES
{
   /**
    * This state is the initial state for a remote node context.  On a resume
    * request the remote node context will transition to the posting state.
    */
   SCIC_SDS_REMOTE_NODE_CONTEXT_INITIAL_STATE,

   /**
    * This is a transition state that posts the RNi to the hardware. Once the RNC
    * is posted the remote node context will be made ready.
    */
   SCIC_SDS_REMOTE_NODE_CONTEXT_POSTING_STATE,

   /**
    * This is a transition state that will post an RNC invalidate to the
    * hardware.  Once the invalidate is complete the remote node context will
    * transition to the posting state.
    */
   SCIC_SDS_REMOTE_NODE_CONTEXT_INVALIDATING_STATE,

   /**
    * This is a transition state that will post an RNC resume to the hardare.
    * Once the event notification of resume complete is received the remote node
    * context will transition to the ready state.
    */
   SCIC_SDS_REMOTE_NODE_CONTEXT_RESUMING_STATE,

   /**
    * This is the state that the remote node context must be in to accept io
    * request operations.
    */
   SCIC_SDS_REMOTE_NODE_CONTEXT_READY_STATE,

   /**
    * This is the state that the remote node context transitions to when it gets
    * a TX suspend notification from the hardware.
    */
   SCIC_SDS_REMOTE_NODE_CONTEXT_TX_SUSPENDED_STATE,

   /**
    * This is the state that the remote node context transitions to when it gets
    * a TX RX suspend notification from the hardware.
    */
   SCIC_SDS_REMOTE_NODE_CONTEXT_TX_RX_SUSPENDED_STATE,

   /**
    * This state is a wait state for the remote node context that waits for a
    * suspend notification from the hardware.  This state is entered when either
    * there is a request to supend the remote node context or when there is a TC
    * completion where the remote node will be suspended by the hardware.
    */
   SCIC_SDS_REMOTE_NODE_CONTEXT_AWAIT_SUSPENSION_STATE,

   SCIC_SDS_REMOTE_NODE_CONTEXT_MAX_STATES

} SCIS_SDS_REMOTE_NODE_CONTEXT_STATES;

/**
 * @enum
 *
 * This enumeration is used to define the end destination state for the remote
 * node context.
 */
enum SCIC_SDS_REMOTE_NODE_CONTEXT_DESTINATION_STATE
{
   SCIC_SDS_REMOTE_NODE_DESTINATION_STATE_UNSPECIFIED,
   SCIC_SDS_REMOTE_NODE_DESTINATION_STATE_READY,
   SCIC_SDS_REMOTE_NODE_DESTINATION_STATE_FINAL
};

/**
 * @struct SCIC_SDS_REMOTE_NODE_CONTEXT
 *
 * @brief  This structure contains the data associated with the remote
 *         node context object.  The remote node context (RNC) object models
 *         the remote device information necessary to manage the
 *         silicon RNC.
 */
typedef struct SCIC_SDS_REMOTE_NODE_CONTEXT
{
   /**
    * This contains the information used to maintain the loggers for the base
    * state machine.
    */
   SCI_BASE_OBJECT_T parent;

   /**
    * This pointer simply points to the remote device object containing
    * this RNC.
    *
    * @todo Consider making the device pointer the associated object of the
    *       the parent object.
    */
   struct SCIC_SDS_REMOTE_DEVICE * device;

   /**
    * This field indicates the remote node index (RNI) associated with
    * this RNC.
    */
   U16 remote_node_index;

   /**
    * This field is the recored suspension code or the reason for the remote node
    * context suspension.
    */
   U32 suspension_code;

   /**
    * This field is TRUE if the remote node context is resuming from its current
    * state.  This can cause an automatic resume on receiving a suspension
    * notification.
    */
   enum SCIC_SDS_REMOTE_NODE_CONTEXT_DESTINATION_STATE destination_state;

   /**
    * This field contains the callback function that the user requested to be
    * called when the requested state transition is complete.
    */
   SCIC_SDS_REMOTE_NODE_CONTEXT_CALLBACK user_callback;

   /**
    * This field contains the parameter that is called when the user requested
    * state transition is completed.
    */
   void * user_cookie;

   /**
    * This field contains the data for the object's state machine.
    */
   SCI_BASE_STATE_MACHINE_T state_machine;

   SCIC_SDS_REMOTE_NODE_CONTEXT_HANDLERS * state_handlers;

   #ifdef SCI_LOGGING
   /**
    * This field conatins the ready substate machine logger.  The logger will
    * emit a message each time the ready substate machine changes state.
    */
   SCI_BASE_STATE_MACHINE_LOGGER_T state_machine_logger;
   #endif

} SCIC_SDS_REMOTE_NODE_CONTEXT_T;

// ---------------------------------------------------------------------------

extern SCI_BASE_STATE_T
   scic_sds_remote_node_context_state_table[
      SCIC_SDS_REMOTE_NODE_CONTEXT_MAX_STATES];

extern SCIC_SDS_REMOTE_NODE_CONTEXT_HANDLERS
   scic_sds_remote_node_context_state_handler_table[
      SCIC_SDS_REMOTE_NODE_CONTEXT_MAX_STATES];

// ---------------------------------------------------------------------------

void scic_sds_remote_node_context_construct(
   struct SCIC_SDS_REMOTE_DEVICE  * device,
   SCIC_SDS_REMOTE_NODE_CONTEXT_T * rnc,
   U16                              remote_node_index
);

void scic_sds_remote_node_context_construct_buffer(
   SCIC_SDS_REMOTE_NODE_CONTEXT_T * rnc
);

BOOL scic_sds_remote_node_context_is_initialized(
   SCIC_SDS_REMOTE_NODE_CONTEXT_T * rnc
);

BOOL scic_sds_remote_node_context_is_ready(
   SCIC_SDS_REMOTE_NODE_CONTEXT_T * this_rnc
);

#define scic_sds_remote_node_context_set_remote_node_index(rnc, rni) \
   ((rnc)->remote_node_index = (rni))

#define scic_sds_remote_node_context_get_remote_node_index(rcn) \
   ((rnc)->remote_node_index)

#define scic_sds_remote_node_context_event_handler(rnc, event_code) \
   ((rnc)->state_handlers->event_handler(rnc, event_code))

#define scic_sds_remote_node_context_resume(rnc, callback, parameter) \
   ((rnc)->state_handlers->resume_handler(rnc, callback, parameter))

#define scic_sds_remote_node_context_suspend(rnc, suspend_type, callback, parameter) \
    ((rnc)->state_handlers->suspend_handler(rnc, suspend_type, callback, parameter))

#define scic_sds_remote_node_context_destruct(rnc, callback, parameter) \
    ((rnc)->state_handlers->destruct_handler(rnc, callback, parameter))

#define scic_sds_remote_node_context_start_io(rnc, request) \
   ((rnc)->state_handlers->start_io_handler(rnc, request))

#define scic_sds_remote_node_context_start_task(rnc, task) \
   ((rnc)->state_handlers->start_task_handler(rnc, task))

// ---------------------------------------------------------------------------

#ifdef SCI_LOGGING
void scic_sds_remote_node_context_initialize_state_logging(
   SCIC_SDS_REMOTE_NODE_CONTEXT_T *this_rnc
);

void scic_sds_remote_node_context_deinitialize_state_logging(
   SCIC_SDS_REMOTE_NODE_CONTEXT_T *this_rnc
);
#else // SCI_LOGGING
#define scic_sds_remote_node_context_initialize_state_logging(x)
#define scic_sds_remote_node_context_deinitialize_state_logging(x)
#endif // SCI_LOGGING

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIC_SDS_REMOTE_NODE_CONTEXT_H_

