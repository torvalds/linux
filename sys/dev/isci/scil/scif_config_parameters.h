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
#ifndef _SCIF_SAS_USER_PARAMETERS_H_
#define _SCIF_SAS_USER_PARAMETERS_H_

/**
 * @file
 *
 * @brief This file contains all of the interface methods that can be called
 *        by an SCIF user on a SCIF_SAS_USER_PARAMETERS object.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_types.h>
#include <dev/isci/scil/sci_status.h>
#include <dev/isci/scil/intel_sas.h>


/**
 * @struct SCIF_SAS_USER_PARAMETERS
 *
 * @brief This structure delineates the various user parameters that can be
 *        changed by the framework user.
 */
typedef struct SCIF_SAS_USER_PARAMETERS
{
   /**
    * This field indicates if the user would like to have the SATA NCQ
    * feature enabled for all remote devices.
    */
   BOOL  is_sata_ncq_enabled;

   /**
    * This field indicates if the user would like to have the SATA Automatic
    * Standby Timer feature enabled for all remote devices.
    */
   BOOL  is_sata_standby_timer_enabled;

   /**
    * This field indicates if the user would like to have the SATA Non-zero
    * Buffer Offset feature enabled for all remote devices.
    */
   BOOL  is_non_zero_buffer_offsets_enabled;

   /**
    * This field indicates if the user would like to clear affiliation for EA
    * SATA devices during the controller stop process.
    */
   BOOL  clear_affiliation_during_controller_stop;

   /**
    * This field indicates the user's desired NCQ depth for all remote
    * devices.  The maximum legal value for this field is 32.
    */
   U16  max_ncq_depth;

   /**
    * This field indicates the type of reset to be applied to all remote
    * devices the first time they are discovered.
    */
   SCI_SAS_TASK_MGMT_FUNCTION_T  reset_type;

   /**
    * This field indicates the os/user recommends ignoring fua in translation
    * for performance reasons.
    */
   BOOL  ignore_fua;

} SCIF_SAS_USER_PARAMETERS_T;

/**
 * @union SCIF_USER_PARAMETERS
 * @brief This structure/union specifies the various different user
 *        parameter sets available.  Each type is specific to a
 *        Serial Attached SCSI implementation of the framework.
 *
 */
typedef union SCIF_USER_PARAMETERS
{
   SCIF_SAS_USER_PARAMETERS_T sas;

} SCIF_USER_PARAMETERS_T;

/**
 * @brief This method allows the user to attempt to change the user
 *        parameters utilized by the controller.
 *
 * @param[in] controller This parameter specifies the controller on which
 *            to set the user parameters.
 * @param[in] user_parameters This parameter specifies the USER_PARAMETERS
 *            object containing the potential new values.
 *
 * @return Indicate if the update of the user parameters was successful.
 * @retval SCI_SUCCESS This value is returned if the operation succeeded.
 * @retval SCI_FAILURE_INVALID_STATE This value is returned if the attempt
 *         to change the user parameter failed, because changing one of
 *         the parameters is not currently allowed.
 * @retval SCI_FAILURE_INVALID_PARAMETER_VALUE This value is returned if the
 *         user supplied an invalid reset_type, ncq depth, etc.
 */
SCI_STATUS scif_user_parameters_set(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCIF_USER_PARAMETERS_T * user_parameters
);

/**
 * @brief This method allows the user to retrieve the user parameters
 *        utilized by the controller.
 *
 * @param[in] controller This parameter specifies the controller on which
 *            to set the user parameters.
 * @param[in] user_parameters This parameter specifies the USER_PARAMETERS
 *            object into which the framework shall save it's parameters.
 *
 * @return none
 */
void scif_user_parameters_get(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCIF_USER_PARAMETERS_T * user_parameters
);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIF_SAS_USER_PARAMETERS_H_

