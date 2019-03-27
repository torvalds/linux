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
#ifndef _SCIF_SAS_IO_REQUEST_H_
#define _SCIF_SAS_IO_REQUEST_H_

/**
 * @file
 *
 * @brief This file contains the protected interface structures, constants,
 *        and methods for the SCIF_SAS_IO_REQUEST object.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/scif_io_request.h>
#include <dev/isci/scil/sci_base_request.h>
#include <dev/isci/scil/scif_sas_request.h>
#include <dev/isci/scil/scif_sas_stp_io_request.h>
#include <dev/isci/scil/intel_sas.h>


struct SCIF_SAS_CONTROLLER;
struct SCIF_SAS_REMOTE_DEVICE;

//Note 0xFF is the maximum possible value to IO_RETRY_LIMIT since the io_retry_count in
//SCIF_SAS_IO_REQUEST is in type of U8.
#define SCIF_SAS_IO_RETRY_LIMIT 0xFF

/**
 * @struct SCIF_SAS_IO_REQUEST
 *
 * @brief The SCI SAS Framework IO request object abstracts the SAS IO
 *        level behavior for the framework component.  Additionally,
 *        it provides a higher level of abstraction for the core IO request
 *        object.
 */
typedef struct SCIF_SAS_IO_REQUEST
{
   /**
    * The SCI_BASE_REQUEST is the parent object for the
    * SCIF_SAS_IO_REQUEST object.
    */
   SCIF_SAS_REQUEST_T  parent;

   /**
    * This field specifies the number of bytes to be utilized for this
    * IO request.  This field is utilized during internal IO requests.
    */
   U32  transfer_length;

   /**
    * This field keeps track of how many times an io got retried.
    */
   U8 retry_count;

} SCIF_SAS_IO_REQUEST_T;

extern SCI_BASE_STATE_T scif_sas_io_request_state_table[];
extern SCI_BASE_REQUEST_STATE_HANDLER_T
   scif_sas_io_request_state_handler_table[];

SCI_STATUS scif_sas_io_request_constructed_start_handler(
   SCI_BASE_REQUEST_T * io_request
);

SCI_STATUS scif_sas_io_request_constructed_abort_handler(
   SCI_BASE_REQUEST_T * io_request
);

SCI_STATUS scif_sas_io_request_default_complete_handler(
   SCI_BASE_REQUEST_T * io_request
);

SCI_STATUS scif_sas_io_request_default_destruct_handler(
   SCI_BASE_REQUEST_T * io_request
);

SCI_STATUS scif_sas_io_request_construct_smp(
   struct SCIF_SAS_CONTROLLER       * fw_controller,
   struct SCIF_SAS_REMOTE_DEVICE    * fw_device,
   void                             * fw_io_memory,
   void                             * core_io_memory,
   U16                                io_tag,
   SMP_REQUEST_T                    * smp_command,
   void                             * user_request_object
);

SCI_STATUS scif_sas_io_request_continue(
   struct SCIF_SAS_CONTROLLER       * fw_controller,
   struct SCIF_SAS_REMOTE_DEVICE    * fw_device,
   SCIF_SAS_REQUEST_T               * fw_request
);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIF_SAS_IO_REQUEST_H_

