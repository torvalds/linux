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
#ifndef _SCI_BASE_REQUST_H_
#define _SCI_BASE_REQUST_H_

/**
 * @file
 *
 * @brief This file contains all of the constants, types, and method
 *        declarations for the SCI base IO and task request objects.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_base_logger.h>
#include <dev/isci/scil/sci_base_state_machine.h>
#include <dev/isci/scil/sci_base_state_machine_logger.h>

/**
 * @enum SCI_BASE_REQUEST_STATES
 *
 * @brief This enumeration depicts all the states for the common request
 *        state machine.
 */
typedef enum _SCI_BASE_REQUEST_STATES
{
   /**
    * Simply the initial state for the base request state machine.
    */
   SCI_BASE_REQUEST_STATE_INITIAL,

   /**
    * This state indicates that the request has been constructed. This state
    * is entered from the INITIAL state.
    */
   SCI_BASE_REQUEST_STATE_CONSTRUCTED,

   /**
    * This state indicates that the request has been started. This state is
    * entered from the CONSTRUCTED state.
    */
   SCI_BASE_REQUEST_STATE_STARTED,

   /**
    * This state indicates that the request has completed.
    * This state is entered from the STARTED state. This state is entered from
    * the ABORTING state.
    */
   SCI_BASE_REQUEST_STATE_COMPLETED,

   /**
    * This state indicates that the request is in the process of being
    * terminated/aborted.
    * This state is entered from the CONSTRUCTED state.
    * This state is entered from the STARTED state.
    */
   SCI_BASE_REQUEST_STATE_ABORTING,

   /**
    * Simply the final state for the base request state machine.
    */
   SCI_BASE_REQUEST_STATE_FINAL,

   SCI_BASE_REQUEST_MAX_STATES

} SCI_BASE_REQUEST_STATES;

/**
 * @struct SCI_BASE_REQUEST
 *
 * @brief The base request object abstracts the fields common to all SCI IO
 *        and task request objects.
 */
typedef struct SCI_BASE_REQUEST
{
   /**
    * The field specifies that the parent object for the base request is the
    * base object itself.
    */
   SCI_BASE_OBJECT_T parent;

   /**
    * This field contains the information for the base request state machine.
    */
   SCI_BASE_STATE_MACHINE_T state_machine;

   #ifdef SCI_LOGGING
   SCI_BASE_STATE_MACHINE_LOGGER_T state_machine_logger;
   #endif // SCI_LOGGING

} SCI_BASE_REQUEST_T;

typedef SCI_STATUS (*SCI_BASE_REQUEST_HANDLER_T)(
   SCI_BASE_REQUEST_T * this_request
);

/**
 * @struct SCI_BASE_REQUEST_STATE_HANDLER
 *
 * @brief This structure contains all of the state handler methods common to
 *        base IO and task request state machines.  Handler methods provide
 *        the ability to change the behavior for user requests or
 *        transitions depending on the state the machine is in.
 *
 */
typedef struct SCI_BASE_REQUEST_STATE_HANDLER
{
   /**
    * The start_handler specifies the method invoked when a user attempts to
    * start a request.
    */
   SCI_BASE_REQUEST_HANDLER_T start_handler;

   /**
    * The abort_handler specifies the method invoked when a user attempts to
    * abort a request.
    */
   SCI_BASE_REQUEST_HANDLER_T abort_handler;

   /**
    * The complete_handler specifies the method invoked when a user attempts to
    * complete a request.
    */
   SCI_BASE_REQUEST_HANDLER_T complete_handler;

   /**
    * The destruct_handler specifies the method invoked when a user attempts to
    * destruct a request.
    */
   SCI_BASE_REQUEST_HANDLER_T destruct_handler;

} SCI_BASE_REQUEST_STATE_HANDLER_T;

/**
 * @brief Construct the base request.
 *
 * @param[in] this_request This parameter specifies the base request
 *            to be constructed.
 * @param[in] logger This parameter specifies the logger associated with
 *            this base request object.
 * @param[in] state_table This parameter specifies the table of state
 *            definitions to be utilized for the request state machine.
 *
 * @return none
 */
void sci_base_request_construct(
   SCI_BASE_REQUEST_T    * this_request,
   SCI_BASE_LOGGER_T     * logger,
   SCI_BASE_STATE_T      * state_table
);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCI_BASE_REQUST_H_
