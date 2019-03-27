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
#ifndef _SCIF_SAS_REQUEST_H_
#define _SCIF_SAS_REQUEST_H_

/**
 * @file
 *
 * @brief This file contains the protected interface structures, constants,
 *        and methods for the SCIF_SAS_REQUEST object.  This object provides
 *        the common data and behavior to SAS IO and task management
 *        request types.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sati_translator_sequence.h>
#include <dev/isci/scil/sci_types.h>
#include <dev/isci/scil/sci_status.h>
#include <dev/isci/scil/sci_fast_list.h>
#include <dev/isci/scil/sci_base_request.h>

#define SCIF_SAS_RESPONSE_DATA_LENGTH 120

struct SCIF_SAS_CONTROLLER;
struct SCIF_SAS_REMOTE_DEVICE;
struct SCIF_SAS_TASK_REQUEST;
struct SCIF_SAS_REQUEST;

typedef SCI_STATUS (*SCIF_SAS_REQUEST_COMPLETION_HANDLER_T)(
   struct SCIF_SAS_CONTROLLER *,
   struct SCIF_SAS_REMOTE_DEVICE *,
   struct SCIF_SAS_REQUEST *,
   SCI_STATUS *
);

/**
 * @struct SCIF_SAS_STP_REQUEST
 *
 * @brief This structure contains all of the data specific to performing
 *        SATA/STP IO and TASK requests.
 */
typedef struct SCIF_SAS_STP_REQUEST
{
   /**
    * This field contains the translation information utilized by SATI.
    * For more information on this field please refer to
    * SATI_TRANSLATOR_SEQUENCE.
    */
   SATI_TRANSLATOR_SEQUENCE_T  sequence;

   /**
    * This field contains the ncq tag being utilized by this IO request.
    * The NCQ tag value must be less than or equal to 31 (0 <= tag <= 31).
    */
   U8  ncq_tag;

} SCIF_SAS_STP_REQUEST_T;

/**
 * @struct SCIF_SAS_REQUEST
 *
 * @brief The SCIF_SAS_REQUEST object abstracts the common SAS
 *        IO & task management data and behavior for the framework component.
 */
typedef struct SCIF_SAS_REQUEST
{
   /**
    * All SAS request types (IO or Task management) have the SCI base
    * request as their parent object.
    */
   SCI_BASE_REQUEST_T  parent;

   /**
    * This field references the list of state specific handler methods to
    * be utilized for this request instance.
    */
   SCI_BASE_REQUEST_STATE_HANDLER_T * state_handlers;

   SCIF_SAS_REQUEST_COMPLETION_HANDLER_T protocol_complete_handler;

   /**
    * This field is utilized to communicate state information relating
    * to this IO request and it's state transitions.
    */
   SCI_STATUS  status;

   /**
    * This field represents the remote device object to which this IO
    * request is destined.
    */
   struct SCIF_SAS_REMOTE_DEVICE * device;

   /**
    * This field references the request object that has asked that this
    * request be terminated.
    */
   struct SCIF_SAS_TASK_REQUEST * terminate_requestor;

   /**
    * This field provides list specific information that enables a request
    * to be placed in a list.
    */
   SCI_FAST_LIST_ELEMENT_T  list_element;

   /**
    * This field indicates if the current request is one internally
    * generated by the framework or if it is a user IO/task request.
    */
   BOOL is_internal;

   /**
    * This field indicates the current request is a high priority one.
    * An internal request is always high priority. But an external request
    * could be high priority.
    */
   BOOL is_high_priority;

   /**
    * This field indicates the current request should not be completed
    * until a pending abort task set request is completed.  For NCQ errors,
    * it will allow waiting until the read log ext data is returned to
    * to determine how to fail/abort the pending ios.
    */
   BOOL is_waiting_for_abort_task_set;

   /**
    * This field indicates the logical unit (LUN) for the request.
    * This field is utilized during internal IO requests.
    */
   U32  lun;

   /**
    * This field specifies sata specific data for the reqeust object.
    * This data is only valid for SATA requests.
    */
   SCIF_SAS_STP_REQUEST_T  stp;

   /**
    * This field contains the handle for the SCI Core request object that is
    * managed by this framework request.
    */
   SCI_IO_REQUEST_HANDLE_T  core_object;

} SCIF_SAS_REQUEST_T;

void scif_sas_request_construct(
   SCIF_SAS_REQUEST_T            * fw_request,
   struct SCIF_SAS_REMOTE_DEVICE * fw_device,
   SCI_BASE_LOGGER_T             * logger,
   SCI_BASE_STATE_T              * state_table
);

SCI_STATUS scif_sas_request_terminate_start(
   SCIF_SAS_REQUEST_T      * fw_request,
   SCI_IO_REQUEST_HANDLE_T   core_request
);

void scif_sas_request_terminate_complete(
   SCIF_SAS_REQUEST_T * fw_request
);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIF_SAS_REQUEST_H_

