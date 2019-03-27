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
#ifndef _SCI_BASE_PORT_H_
#define _SCI_BASE_PORT_H_

/**
 * @file
 *
 * @brief This file contains all of the structures, constants, and methods
 *        common to all port object definitions.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_base_object.h>
#include <dev/isci/scil/sci_base_logger.h>
#include <dev/isci/scil/sci_base_state_machine.h>
#include <dev/isci/scil/sci_base_state_machine_logger.h>

/**
 * @enum SCI_BASE_PORT_STATES
 *
 * @brief This enumeration depicts all the states for the common port
 *        state machine.
 */
typedef enum _SCI_BASE_PORT_STATES
{
   /**
    * This state indicates that the port has successfully been stopped.
    * In this state no new IO operations are permitted.
    * This state is entered from the STOPPING state.
    */
   SCI_BASE_PORT_STATE_STOPPED,

   /**
    * This state indicates that the port is in the process of stopping.
    * In this state no new IO operations are permitted, but existing IO
    * operations are allowed to complete.
    * This state is entered from the READY state.
    */
   SCI_BASE_PORT_STATE_STOPPING,

   /**
    * This state indicates the port is now ready.  Thus, the user is
    * able to perform IO operations on this port.
    * This state is entered from the STARTING state.
    */
   SCI_BASE_PORT_STATE_READY,

   /**
    * This state indicates the port is in the process of performing a hard
    * reset.  Thus, the user is unable to perform IO operations on this
    * port.
    * This state is entered from the READY state.
    */
   SCI_BASE_PORT_STATE_RESETTING,

   /**
    * This state indicates the port has failed a reset request.  This state
    * is entered when a port reset request times out.
    * This state is entered from the RESETTING state.
    */
   SCI_BASE_PORT_STATE_FAILED,

   SCI_BASE_PORT_MAX_STATES

} SCI_BASE_PORT_STATES;

/**
 * @struct SCI_BASE_PORT
 *
 * @brief The base port object abstracts the fields common to all SCI
 *        port objects.
 */
typedef struct SCI_BASE_PORT
{
   /**
    * The field specifies that the parent object for the base controller
    * is the base object itself.
    */
   SCI_BASE_OBJECT_T parent;

   /**
    * This field contains the information for the base port state machine.
    */
   SCI_BASE_STATE_MACHINE_T state_machine;

   #ifdef SCI_LOGGING
   SCI_BASE_STATE_MACHINE_LOGGER_T state_machine_logger;
   #endif // SCI_LOGGING

} SCI_BASE_PORT_T;

struct SCI_BASE_PHY;

typedef SCI_STATUS (*SCI_BASE_PORT_HANDLER_T)(
   SCI_BASE_PORT_T *
);

typedef SCI_STATUS (*SCI_BASE_PORT_PHY_HANDLER_T)(
   SCI_BASE_PORT_T *,
   struct SCI_BASE_PHY *
);

typedef SCI_STATUS (*SCI_BASE_PORT_RESET_HANDLER_T)(
   SCI_BASE_PORT_T *,
   U32 timeout
);

/**
 * @struct SCI_BASE_PORT_STATE_HANDLER
 *
 * @brief This structure contains all of the state handler methods common to
 *        base port state machines.  Handler methods provide the ability
 *        to change the behavior for user requests or transitions depending
 *        on the state the machine is in.
 */
typedef struct SCI_BASE_PORT_STATE_HANDLER
{
   /**
    * The start_handler specifies the method invoked when a user attempts to
    * start a port.
    */
   SCI_BASE_PORT_HANDLER_T start_handler;

   /**
    * The stop_handler specifies the method invoked when a user attempts to
    * stop a port.
    */
   SCI_BASE_PORT_HANDLER_T stop_handler;

   /**
    * The destruct_handler specifies the method invoked when attempting to
    * destruct a port.
    */
   SCI_BASE_PORT_HANDLER_T destruct_handler;

   /**
    * The reset_handler specifies the method invoked when a user attempts to
    * hard reset a port.
    */
   SCI_BASE_PORT_RESET_HANDLER_T reset_handler;

   /**
    * The add_phy_handler specifies the method invoked when a user attempts to
    * add another phy into the port.
    */
   SCI_BASE_PORT_PHY_HANDLER_T add_phy_handler;

   /**
    * The remove_phy_handler specifies the method invoked when a user
    * attempts to remove a phy from the port.
    */
   SCI_BASE_PORT_PHY_HANDLER_T remove_phy_handler;

} SCI_BASE_PORT_STATE_HANDLER_T;

/**
 * @brief Construct the base port object
 *
 * @param[in] this_port This parameter specifies the base port to be
 *            constructed.
 * @param[in] logger This parameter specifies the logger to be associated
 *            with this base port object.
 * @param[in] state_table This parameter specifies the table of state
 *            definitions to be utilized for the domain state machine.
 *
 * @return none
 */
void sci_base_port_construct(
   SCI_BASE_PORT_T   * this_port,
   SCI_BASE_LOGGER_T * logger,
   SCI_BASE_STATE_T  * state_table
);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCI_BASE_PORT_H_
