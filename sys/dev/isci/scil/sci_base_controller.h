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
#ifndef _SCI_BASE_CONTROLLER_H_
#define _SCI_BASE_CONTROLLER_H_

/**
 * @file
 *
 * @brief This file contains all of the structures, constants, and methods
 *        common to all controller object definitions.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/intel_sas.h>
#include <dev/isci/scil/sci_controller_constants.h>

#include <dev/isci/scil/sci_base_object.h>
#include <dev/isci/scil/sci_base_state.h>
#include <dev/isci/scil/sci_base_logger.h>
#include <dev/isci/scil/sci_base_memory_descriptor_list.h>
#include <dev/isci/scil/sci_base_state_machine.h>
#include <dev/isci/scil/sci_base_state_machine_logger.h>

/**
 * @enum SCI_BASE_CONTROLLER_STATES
 *
 * @brief This enumeration depicts all the states for the common controller
 *        state machine.
 */
typedef enum _SCI_BASE_CONTROLLER_STATES
{
   /**
    * Simply the initial state for the base controller state machine.
    */
   SCI_BASE_CONTROLLER_STATE_INITIAL = 0,

   /**
    * This state indicates that the controller is reset.  The memory for
    * the controller is in its initial state, but the controller requires
    * initialization.
    * This state is entered from the INITIAL state.
    * This state is entered from the RESETTING state.
    */
   SCI_BASE_CONTROLLER_STATE_RESET,

   /**
    * This state is typically an action state that indicates the controller
    * is in the process of initialization.  In this state no new IO operations
    * are permitted.
    * This state is entered from the RESET state.
    */
   SCI_BASE_CONTROLLER_STATE_INITIALIZING,

   /**
    * This state indicates that the controller has been successfully
    * initialized.  In this state no new IO operations are permitted.
    * This state is entered from the INITIALIZING state.
    */
   SCI_BASE_CONTROLLER_STATE_INITIALIZED,

   /**
    * This state indicates the controller is in the process of becoming
    * ready (i.e. starting).  In this state no new IO operations are permitted.
    * This state is entered from the INITIALIZED state.
    */
   SCI_BASE_CONTROLLER_STATE_STARTING,

   /**
    * This state indicates the controller is now ready.  Thus, the user
    * is able to perform IO operations on the controller.
    * This state is entered from the STARTING state.
    */
   SCI_BASE_CONTROLLER_STATE_READY,

   /**
    * This state is typically an action state that indicates the controller
    * is in the process of resetting.  Thus, the user is unable to perform
    * IO operations on the controller.  A reset is considered destructive in
    * most cases.
    * This state is entered from the READY state.
    * This state is entered from the FAILED state.
    * This state is entered from the STOPPED state.
    */
   SCI_BASE_CONTROLLER_STATE_RESETTING,

   /**
    * This state indicates that the controller is in the process of stopping.
    * In this state no new IO operations are permitted, but existing IO
    * operations are allowed to complete.
    * This state is entered from the READY state.
    */
   SCI_BASE_CONTROLLER_STATE_STOPPING,

   /**
    * This state indicates that the controller has successfully been stopped.
    * In this state no new IO operations are permitted.
    * This state is entered from the STOPPING state.
    */
   SCI_BASE_CONTROLLER_STATE_STOPPED,

   /**
    * This state indicates that the controller could not successfully be
    * initialized.  In this state no new IO operations are permitted.
    * This state is entered from the INITIALIZING state.
    * This state is entered from the STARTING state.
    * This state is entered from the STOPPING state.
    * This state is entered from the RESETTING state.
    */
   SCI_BASE_CONTROLLER_STATE_FAILED,

   SCI_BASE_CONTROLLER_MAX_STATES

} SCI_BASE_CONTROLLER_STATES;

/**
 * @struct SCI_BASE_CONTROLLER
 *
 * @brief The base controller object abstracts the fields common to all
 *        SCI controller objects.
 */
typedef struct SCI_BASE_CONTROLLER
{
   /**
    * The field specifies that the parent object for the base controller
    * is the base object itself.
    */
   SCI_BASE_OBJECT_T parent;

   /**
    * This field points to the memory descriptor list associated with this
    * controller.  The MDL indicates the memory requirements necessary for
    * this controller object.
    */
   SCI_BASE_MEMORY_DESCRIPTOR_LIST_T  mdl;

   /**
    * This field records the fact that the controller has encountered a fatal memory
    * error and controller must stay in failed state.
    */
   U8 error;

   /**
    * This field contains the information for the base controller state
    * machine.
    */
   SCI_BASE_STATE_MACHINE_T state_machine;

   #ifdef SCI_LOGGING
   SCI_BASE_STATE_MACHINE_LOGGER_T state_machine_logger;
   #endif // SCI_LOGGING

} SCI_BASE_CONTROLLER_T;

// Forward declarations
struct SCI_BASE_REMOTE_DEVICE;
struct SCI_BASE_REQUEST;

typedef SCI_STATUS (*SCI_BASE_CONTROLLER_HANDLER_T)(
   SCI_BASE_CONTROLLER_T *
);

typedef SCI_STATUS (*SCI_BASE_CONTROLLER_TIMED_HANDLER_T)(
   SCI_BASE_CONTROLLER_T *,
   U32
);

typedef SCI_STATUS (*SCI_BASE_CONTROLLER_REQUEST_HANDLER_T)(
   SCI_BASE_CONTROLLER_T *,
   struct SCI_BASE_REMOTE_DEVICE *,
   struct SCI_BASE_REQUEST *
);

typedef SCI_STATUS (*SCI_BASE_CONTROLLER_START_REQUEST_HANDLER_T)(
   SCI_BASE_CONTROLLER_T *,
   struct SCI_BASE_REMOTE_DEVICE *,
   struct SCI_BASE_REQUEST *,
   U16
);


/**
 * @struct SCI_BASE_CONTROLLER_STATE_HANDLER
 *
 * @brief This structure contains all of the state handler methods common to
 *        base controller state machines.  Handler methods provide the ability
 *        to change the behavior for user requests or transitions depending
 *        on the state the machine is in.
 */
typedef struct SCI_BASE_CONTROLLER_STATE_HANDLER
{
   /**
    * The start_handler specifies the method invoked when a user attempts to
    * start a controller.
    */
   SCI_BASE_CONTROLLER_TIMED_HANDLER_T start_handler;

   /**
    * The stop_handler specifies the method invoked when a user attempts to
    * stop a controller.
    */
   SCI_BASE_CONTROLLER_TIMED_HANDLER_T stop_handler;

   /**
    * The reset_handler specifies the method invoked when a user attempts to
    * reset a controller.
    */
   SCI_BASE_CONTROLLER_HANDLER_T reset_handler;

   /**
    * The initialize_handler specifies the method invoked when a user
    * attempts to initialize a controller.
    */
   SCI_BASE_CONTROLLER_HANDLER_T initialize_handler;

   /**
    * The start_io_handler specifies the method invoked when a user
    * attempts to start an IO request for a controller.
    */
   SCI_BASE_CONTROLLER_START_REQUEST_HANDLER_T start_io_handler;

   /**
    * The start_internal_request_handler specifies the method invoked when a user
    * attempts to start an internal request for a controller.
    */
   SCI_BASE_CONTROLLER_START_REQUEST_HANDLER_T start_high_priority_io_handler;

   /**
    * The complete_io_handler specifies the method invoked when a user
    * attempts to complete an IO request for a controller.
    */
   SCI_BASE_CONTROLLER_REQUEST_HANDLER_T complete_io_handler;

    /**
    * The complete_high_priority_io_handler specifies the method invoked when a user
    * attempts to complete a high priority IO request for a controller.
    */
   SCI_BASE_CONTROLLER_REQUEST_HANDLER_T complete_high_priority_io_handler;

   /**
    * The continue_io_handler specifies the method invoked when a user
    * attempts to continue an IO request for a controller.
    */
   SCI_BASE_CONTROLLER_REQUEST_HANDLER_T continue_io_handler;

   /**
    * The start_task_handler specifies the method invoked when a user
    * attempts to start a task management request for a controller.
    */
   SCI_BASE_CONTROLLER_START_REQUEST_HANDLER_T start_task_handler;

   /**
    * The complete_task_handler specifies the method invoked when a user
    * attempts to complete a task management request for a controller.
    */
   SCI_BASE_CONTROLLER_REQUEST_HANDLER_T complete_task_handler;

} SCI_BASE_CONTROLLER_STATE_HANDLER_T;

/**
 * @brief Construct the base controller
 *
 * @param[in] this_controller This parameter specifies the base controller
 *            to be constructed.
 * @param[in] logger This parameter specifies the logger associated with
 *            this base controller object.
 * @param[in] state_table This parameter specifies the table of state
 *            definitions to be utilized for the controller state machine.
 * @param[in] mde_array This parameter specifies the array of memory
 *            descriptor entries to be managed by this list.
 * @param[in] mde_array_length This parameter specifies the size of the
 *            array of entries.
 * @param[in] next_mdl This parameter specifies a subsequent MDL object
 *            to be managed by this MDL object.
 * @param[in] oem_parameters This parameter specifies the original
 *            equipment manufacturer parameters to be utilized by this
 *            controller object.
 *
 * @return none
 */
void sci_base_controller_construct(
   SCI_BASE_CONTROLLER_T             * this_controller,
   SCI_BASE_LOGGER_T                 * logger,
   SCI_BASE_STATE_T                  * state_table,
   SCI_PHYSICAL_MEMORY_DESCRIPTOR_T  * mdes,
   U32                                 mde_count,
   SCI_MEMORY_DESCRIPTOR_LIST_HANDLE_T next_mdl
);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCI_BASE_CONTROLLER_H_
