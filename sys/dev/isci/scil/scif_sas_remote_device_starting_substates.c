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
 * @brief This file contains the entrance and exit methods for the starting
 *        sub-state machine states (AWAIT COMPELTE).  The starting sub-state
 *        machine manages the steps necessary to initialize and configure
 *        a remote device.
 */

#include <dev/isci/scil/scif_sas_remote_device.h>
#include <dev/isci/scil/scif_sas_domain.h>
#include <dev/isci/scil/scif_sas_logger.h>


//******************************************************************************
//* P R O T E C T E D   M E T H O D S
//******************************************************************************

/**
 * @brief This method implements the actions taken when entering the
 *        STARTING AWAIT COMPLETE substate.  This includes setting the
 *        state handler methods.
 *
 * @param[in]  object This parameter specifies the base object for which
 *             the state transition is occurring.  This is cast into a
 *             SCIF_SAS_REMOTE_DEVICE object in the method implementation.
 *
 * @return none
 */
static
void scif_sas_remote_device_starting_await_complete_substate_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T *)object;

   SET_STATE_HANDLER(
      fw_device,
      scif_sas_remote_device_starting_substate_handler_table,
      SCIF_SAS_REMOTE_DEVICE_STARTING_SUBSTATE_AWAIT_COMPLETE
   );

   fw_device->domain->device_start_in_progress_count++;
   fw_device->domain->device_start_count++;
}

/**
 * @brief This method implements the actions taken when entering the
 *        STARTING COMPLETE substate.  This includes setting the
 *        state handler methods.
 *
 * @param[in]  object This parameter specifies the base object for which
 *             the state transition is occurring.  This is cast into a
 *             SCIF_SAS_REMOTE_DEVICE object in the method implementation.
 *
 * @return none
 */
static
void scif_sas_remote_device_starting_complete_substate_enter(
   SCI_BASE_OBJECT_T *object
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T *)object;

   SET_STATE_HANDLER(
      fw_device,
      scif_sas_remote_device_starting_substate_handler_table,
      SCIF_SAS_REMOTE_DEVICE_STARTING_SUBSTATE_AWAIT_READY
   );
}


SCI_BASE_STATE_T
scif_sas_remote_device_starting_substate_table
[SCIF_SAS_REMOTE_DEVICE_STARTING_SUBSTATE_MAX_STATES] =
{
   {
      SCIF_SAS_REMOTE_DEVICE_STARTING_SUBSTATE_AWAIT_COMPLETE,
      scif_sas_remote_device_starting_await_complete_substate_enter,
      NULL
   },
   {
      SCIF_SAS_REMOTE_DEVICE_STARTING_SUBSTATE_AWAIT_READY,
      scif_sas_remote_device_starting_complete_substate_enter,
      NULL
   }
};

