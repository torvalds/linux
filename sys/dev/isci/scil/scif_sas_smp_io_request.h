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
#ifndef _SCIF_SAS_SMP_REQUEST_H_
#define _SCIF_SAS_SMP_REQUEST_H_

/**
 * @file
 *
 * @brief This file contains the protected interface structures, constants,
 *        and methods for the SCIF_SAS_SMP_REQUEST object.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/intel_sas.h>
#include <dev/isci/scil/sci_types.h>
#include <dev/isci/scil/sci_status.h>
#include <dev/isci/scil/sci_base_request.h>

struct SCIF_SAS_REQUEST;
struct SCIF_SAS_IO_REQUEST;
struct SCIF_SAS_CONTROLLER;
struct SCIF_SAS_REMOTE_DEVICE;
struct SCIF_SAS_INTERNAL_IO_REQUEST;

void scif_sas_smp_request_construct(
   struct SCIF_SAS_REQUEST * fw_io,
   SMP_REQUEST_T * smp_command
);

void * scif_sas_smp_request_construct_report_general(
   struct SCIF_SAS_CONTROLLER    * fw_controller,
   struct SCIF_SAS_REMOTE_DEVICE * fw_device
);

void * scif_sas_smp_request_construct_report_manufacturer_info(
   struct SCIF_SAS_CONTROLLER    * fw_controller,
   struct SCIF_SAS_REMOTE_DEVICE * fw_device
);

void * scif_sas_smp_request_construct_discover(
   struct SCIF_SAS_CONTROLLER    * fw_controller,
   struct SCIF_SAS_REMOTE_DEVICE * fw_device,
   U8                              phy_identifier,
   void                          * external_request_object,
   void                          * external_memory
);

void * scif_sas_smp_request_construct_report_phy_sata(
   struct SCIF_SAS_CONTROLLER    * fw_controller,
   struct SCIF_SAS_REMOTE_DEVICE * fw_device,
   U8                              phy_identifier
);

void * scif_sas_smp_request_construct_phy_control(
   struct SCIF_SAS_CONTROLLER    * fw_controller,
   struct SCIF_SAS_REMOTE_DEVICE * fw_device,
   U8                         phy_operation,
   U8                         phy_identifier,
   void                     * external_request_object,
   void                     * external_memory
);

void * scif_sas_smp_request_construct_config_route_info(
   struct SCIF_SAS_CONTROLLER    * fw_controller,
   struct SCIF_SAS_REMOTE_DEVICE * fw_device,
   U8                              phy_id,
   U16                             route_index,
   SCI_SAS_ADDRESS_T               destination_sas_Address,
   BOOL                            disable_expander_route_entry
);

SCI_STATUS scif_sas_smp_internal_request_retry(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device
);

SCI_STATUS scif_sas_smp_external_request_retry(
   struct SCIF_SAS_IO_REQUEST    * old_io
);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif
