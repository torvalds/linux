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
#ifndef _SCI_BASE_DOMAIN_H_
#define _SCI_BASE_DOMAIN_H_

/**
 * @file
 *
 * @brief This file contains all of the structures, constants, and methods
 *        common to all domain object definitions.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_base_object.h>
#include <dev/isci/scil/sci_base_logger.h>
#include <dev/isci/scil/sci_base_state_machine.h>
#include <dev/isci/scil/sci_base_state_machine_logger.h>

/**
 * @enum SCI_BASE_DOMAIN_STATES
 *
 * @brief This enumeration depicts the standard states common to all domain
 *        state machine implementations.
 */
typedef enum _SCI_BASE_DOMAIN_STATES
{
   /**
    * Simply the initial state for the base domain state machine.
    */
   SCI_BASE_DOMAIN_STATE_INITIAL,

   /**
    * This state indicates that the domain has successfully been stopped.
    * In this state no new IO operations are permitted.
    * This state is entered from the INITIAL state.
    * This state is entered from the DISCOVERING state.
    */
   SCI_BASE_DOMAIN_STATE_STARTING,

   /**
    * This state indicates the domain is now ready.  Thus, the user
    * is able to perform IO operations to remote devices in this domain.
    * This state is entered from the STOPPED state.
    * This state is entered from the STOPPING state.
    * This state is entered from the DISCOVERING state.
    */
   SCI_BASE_DOMAIN_STATE_READY,

   /**
    * This state indicates that the domain is in the process of stopping.
    * In this state no new IO operations are permitted, but existing IO
    * operations in the domain are allowed to complete.
    * This state is entered from the READY state.
    * This state is entered from the DISCOVERING state.
    */
   SCI_BASE_DOMAIN_STATE_STOPPING,

   /**
    * This state indicates that the domain has successfully been stopped.
    * In this state no new IO operations are permitted.
    * This state is entered from the INITIAL state.
    * This state is entered from the STOPPING state.
    */
   SCI_BASE_DOMAIN_STATE_STOPPED,

   /**
    * This state indicates that the domain is actively attempting to
    * discover what remote devices are contained in it.  In this state no
    * new user IO requests are permitted.
    * This state is entered from the READY state.
    */
   SCI_BASE_DOMAIN_STATE_DISCOVERING,

   SCI_BASE_DOMAIN_MAX_STATES

} SCI_BASE_DOMAIN_STATES;

/**
 * @struct SCI_BASE_DOMAIN
 *
 * @brief This structure defines all of the fields common to DOMAIN objects.
 */
typedef struct SCI_BASE_DOMAIN
{
   /**
    * This field depicts the parent object (SCI_BASE_OBJECT) for the domain.
    */
   SCI_BASE_OBJECT_T parent;

   /**
    * This field contains the information for the base domain state machine.
    */
   SCI_BASE_STATE_MACHINE_T state_machine;

   #ifdef SCI_LOGGING
   SCI_BASE_STATE_MACHINE_LOGGER_T state_machine_logger;
   #endif // SCI_LOGGING

} SCI_BASE_DOMAIN_T;

struct SCI_BASE_CONTROLLER;
struct SCI_BASE_REMOTE_DEVICE;
struct SCI_BASE_REQUEST;
struct SCI_BASE_REQUEST;

typedef SCI_STATUS (*SCI_BASE_DOMAIN_TIMED_HANDLER_T)(
   SCI_BASE_DOMAIN_T *,
   U32,
   U32
);

typedef SCI_STATUS (*SCI_BASE_DOMAIN_HANDLER_T)(
   SCI_BASE_DOMAIN_T *
);

typedef SCI_STATUS (*SCI_BASE_DOMAIN_PORT_NOT_READY_HANDLER_T)(
   SCI_BASE_DOMAIN_T *,
   U32
);

typedef SCI_STATUS (*SCI_BASE_DOMAIN_DEVICE_HANDLER_T)(
   SCI_BASE_DOMAIN_T *,
   struct SCI_BASE_REMOTE_DEVICE *
);

typedef SCI_STATUS (*SCI_BASE_DOMAIN_REQUEST_HANDLER_T)(
   SCI_BASE_DOMAIN_T *,
   struct SCI_BASE_REMOTE_DEVICE *,
   struct SCI_BASE_REQUEST *
);

typedef SCI_STATUS (*SCI_BASE_DOMAIN_HIGH_PRIORITY_REQUEST_COMPLETE_HANDLER_T)(
   SCI_BASE_DOMAIN_T *,
   struct SCI_BASE_REMOTE_DEVICE *,
   struct SCI_BASE_REQUEST *,
   void *,
   SCI_IO_STATUS
);


/**
 * @struct SCI_BASE_DOMAIN_STATE_HANDLER
 *
 * @brief This structure contains all of the state handler methods common to
 *        base domain state machines.  Handler methods provide the ability
 *        to change the behavior for user requests or transitions depending
 *        on the state the machine is in.
 */
typedef struct SCI_BASE_DOMAIN_STATE_HANDLER
{
   /**
    * The discover_handler specifies the method invoked when a user attempts
    * to discover a domain.
    */
   SCI_BASE_DOMAIN_TIMED_HANDLER_T discover_handler;

   /**
    * The port_ready_handler specifies the method invoked an SCI Core
    * informs the domain object that it's associated port is now ready
    * for IO operation.
    */
   SCI_BASE_DOMAIN_HANDLER_T port_ready_handler;

   /**
    * The port_not_ready_handler specifies the method invoked an SCI Core
    * informs the domain object that it's associated port is no longer ready
    * for IO operation.
    */
   SCI_BASE_DOMAIN_PORT_NOT_READY_HANDLER_T port_not_ready_handler;

   /**
    * The device_start_complete_handler specifies the method invoked when a
    * remote device start operation in the domain completes.
    */
   SCI_BASE_DOMAIN_DEVICE_HANDLER_T device_start_complete_handler;

   /**
    * The device_stop_complete_handler specifies the method invoked when a
    * remote device stop operation in the domain completes.
    */
   SCI_BASE_DOMAIN_DEVICE_HANDLER_T device_stop_complete_handler;

   /**
    * The device_destruct_handler specifies the method invoked when sci user
    * destruct a remote device of this domain.
    */
   SCI_BASE_DOMAIN_DEVICE_HANDLER_T device_destruct_handler;

   /**
    * The start_io_handler specifies the method invoked when a user
    * attempts to start an IO request for a domain.
    */
   SCI_BASE_DOMAIN_REQUEST_HANDLER_T start_io_handler;

   /**
    * The start_high_priority_io_handler specifies the method invoked when a user
    * attempts to start an high priority request for a domain.
    */
   SCI_BASE_DOMAIN_REQUEST_HANDLER_T start_high_priority_io_handler;

   /**
    * The complete_io_handler specifies the method invoked when a user
    * attempts to complete an IO request for a domain.
    */
   SCI_BASE_DOMAIN_REQUEST_HANDLER_T complete_io_handler;

   /**
    * The complete_high_priority_io_handler specifies the method invoked when a
    * user attempts to complete an high priority IO request for a domain.
    */
   SCI_BASE_DOMAIN_HIGH_PRIORITY_REQUEST_COMPLETE_HANDLER_T complete_high_priority_io_handler;

   /**
    * The continue_io_handler specifies the method invoked when a user
    * attempts to continue an IO request for a domain.
    */
   SCI_BASE_DOMAIN_REQUEST_HANDLER_T continue_io_handler;

   /**
    * The start_task_handler specifies the method invoked when a user
    * attempts to start a task management request for a domain.
    */
   SCI_BASE_DOMAIN_REQUEST_HANDLER_T start_task_handler;

   /**
    * The complete_task_handler specifies the method invoked when a user
    * attempts to complete a task management request for a domain.
    */
   SCI_BASE_DOMAIN_REQUEST_HANDLER_T complete_task_handler;

} SCI_BASE_DOMAIN_STATE_HANDLER_T;

/**
 * @brief Construct the base domain
 *
 * @param[in] this_domain This parameter specifies the base domain to be
 *            constructed.
 * @param[in] logger This parameter specifies the logger associated with
 *            this base domain object.
 * @param[in] state_table This parameter specifies the table of state
 *            definitions to be utilized for the domain state machine.
 *
 * @return none
 */
void sci_base_domain_construct(
   SCI_BASE_DOMAIN_T * this_domain,
   SCI_BASE_LOGGER_T * logger,
   SCI_BASE_STATE_T  * state_table
);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCI_BASE_DOMAIN_H_
