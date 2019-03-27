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
#ifndef _SCIF_SAS_SATI_BINDING_H_
#define _SCIF_SAS_SATI_BINDING_H_

/**
 * @file
 *
 * @brief This file contains the SATI (SCSI to ATA Translation Implementation)
 *        callback implementations that can be implemented by the SCI
 *        Framework (or core in some cases).
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/scif_user_callback.h>
#include <dev/isci/scil/scif_io_request.h>
#include <dev/isci/scil/scif_remote_device.h>
#include <dev/isci/scil/scic_user_callback.h>
#include <dev/isci/scil/scic_io_request.h>
#include <dev/isci/scil/scic_remote_device.h>
//#include <dev/isci/scil/scic_sds_request.h>
//#include <dev/isci/scil/scu_task_context.h>
#include <dev/isci/scil/sci_object.h>
#include <dev/isci/scil/scif_sas_request.h>
#include <dev/isci/scil/sci_base_memory_descriptor_list.h>
#include <dev/isci/scil/scif_sas_stp_remote_device.h>
#include <dev/isci/scil/scif_sas_remote_device.h>
#include <dev/isci/scil/scif_sas_domain.h>
#include <dev/isci/scil/scif_sas_controller.h>
#include <dev/isci/scil/scic_sds_request.h>
#include <dev/isci/sci_environment.h>

// SATI callbacks fulfilled by the framework user.

#define sati_cb_get_data_byte(scsi_io, byte_offset, value)                    \
{                                                                             \
   U8 * virtual_address = scif_cb_io_request_get_virtual_address_from_sgl(    \
                             sci_object_get_association(scsi_io),(byte_offset)\
                          );                                                  \
   *(value) = *(virtual_address);                                             \
}

#define sati_cb_set_data_byte(scsi_io, byte_offset, value)                    \
{                                                                             \
   U8 * virtual_address = scif_cb_io_request_get_virtual_address_from_sgl(    \
                             sci_object_get_association(scsi_io),(byte_offset)\
                          );                                                  \
   *(virtual_address) = value;                                                \
}

#define sati_cb_get_cdb_address(scsi_io) \
   scif_cb_io_request_get_cdb_address(sci_object_get_association(scsi_io))

#define sati_cb_get_cdb_length(scsi_io)  \
   scif_cb_io_request_get_cdb_length(sci_object_get_association(scsi_io))

#define sati_cb_get_data_direction(scsi_io, sati_data_direction)              \
{                                                                             \
   SCI_IO_REQUEST_DATA_DIRECTION sci_data_direction =                         \
      scif_cb_io_request_get_data_direction(                                  \
         sci_object_get_association(scsi_io)                                  \
      );                                                                      \
   if (sci_data_direction == SCI_IO_REQUEST_DATA_IN)                          \
      *(sati_data_direction) = SATI_DATA_DIRECTION_IN;                        \
   else if (sci_data_direction == SCI_IO_REQUEST_DATA_OUT)                    \
      *(sati_data_direction) = SATI_DATA_DIRECTION_OUT;                       \
   else if (sci_data_direction == SCI_IO_REQUEST_NO_DATA)                     \
      *(sati_data_direction) = SATI_DATA_DIRECTION_NONE;                      \
}

#define sati_cb_get_lun(scsi_io) \
   scif_cb_io_request_get_lun(sci_object_get_association(scsi_io))

// SATI callbacks fulfilled by the framework.

/**
 * This method implements the functionality necessary to fulfill the
 * SCSI-to-ATA Translation requirements.  It ensures that the SAS
 * address for the remote device associated with the supplied IO
 * is written into the sas_address parameter.
 * For more information on the parameters utilized in this method,
 * please refer to sati_cb_device_get_sas_address().
 */
#define sati_cb_device_get_sas_address(scsi_io, sas_address)               \
{                                                                          \
   SCIF_SAS_REQUEST_T* fw_request = (SCIF_SAS_REQUEST_T*)scsi_io;          \
   SCI_REMOTE_DEVICE_HANDLE_T scic_device                                  \
      = scif_remote_device_get_scic_handle(fw_request->device);            \
   scic_remote_device_get_sas_address(scic_device, sas_address);           \
}

#define sati_cb_device_get_request_by_ncq_tag(scsi_io, ncq_tag, matching_req) \
{                                                                          \
   SCIF_SAS_REQUEST_T* fw_request = (SCIF_SAS_REQUEST_T*)scsi_io;          \
   SCIF_SAS_REMOTE_DEVICE_T* fw_device = fw_request->device;               \
   matching_req = scif_sas_stp_remote_device_get_request_by_ncq_tag(fw_device, ncq_tag);  \
}

#define sati_cb_io_request_complete(scsi_io, completion_status)            \
{                                                                          \
   SCIF_SAS_REQUEST_T* fw_request = (SCIF_SAS_REQUEST_T*)scsi_io;          \
   SCIF_SAS_REMOTE_DEVICE_T* fw_device = fw_request->device;               \
   SCIF_SAS_DOMAIN_T*  fw_domain = fw_device->domain;                      \
   SCIF_SAS_CONTROLLER_T* fw_controller = fw_domain->controller;           \
   scif_cb_io_request_complete(                                            \
      fw_controller, fw_device, fw_request, completion_status              \
   );                                                                      \
}

#define sati_cb_get_response_iu_address scif_io_request_get_response_iu_address
#define sati_cb_get_task_function       scic_cb_ssp_task_request_get_function

#define sati_cb_get_ata_data_address(the_ata_io)          \
   scic_io_request_get_rx_frame(                          \
      scif_io_request_get_scic_handle((the_ata_io)), 0    \
   )

#define sati_cb_get_h2d_register_fis_address(the_ata_io)  \
   (U8*) scic_stp_io_request_get_h2d_reg_address(         \
            scif_io_request_get_scic_handle((the_ata_io)) \
         )

#define sati_cb_get_d2h_register_fis_address(the_ata_io)  \
   (U8*) scic_stp_io_request_get_d2h_reg_address(         \
            scif_io_request_get_scic_handle((the_ata_io)) \
         )

#define sati_cb_allocate_dma_buffer(scsi_io, length, virt_address, phys_address_low, phys_address_high) \
{                                                                 \
   SCIF_SAS_REQUEST_T* fw_request = (SCIF_SAS_REQUEST_T*)scsi_io; \
   SCI_PHYSICAL_MEMORY_DESCRIPTOR_T mde;                          \
   mde.virtual_address = NULL;                                    \
   sci_base_mde_construct(                                        \
      &mde, 4, length, SCI_MDE_ATTRIBUTE_PHYSICALLY_CONTIGUOUS    \
   );                                                             \
   scif_cb_controller_allocate_memory(                            \
      fw_request->device->domain->controller, &mde                \
   );                                                             \
   *(virt_address)       = mde.virtual_address;                      \
   *(phys_address_low)   = sci_cb_physical_address_lower(mde.physical_address); \
   *(phys_address_high)  = sci_cb_physical_address_upper(mde.physical_address); \
}

#define sati_cb_free_dma_buffer(scsi_io, virt_address)         \
{                                                                 \
   SCIF_SAS_REQUEST_T* fw_request = (SCIF_SAS_REQUEST_T*)scsi_io; \
   SCI_PHYSICAL_MEMORY_DESCRIPTOR_T mde;                          \
   mde.virtual_address = virt_address;                         \
   sci_base_mde_construct(                                        \
      &mde, 4, 0, SCI_MDE_ATTRIBUTE_PHYSICALLY_CONTIGUOUS         \
   );                                                             \
   scif_cb_controller_free_memory(                                \
      fw_request->device->domain->controller, &mde                \
   );                                                             \
}

#define sati_cb_sgl_next_sge(scsi_io, ata_io, current_sge, next_sge) \
{ \
   /* For now just 2 SGEs are supported. */ \
   SCIC_SDS_REQUEST_T *scic_request; \
   SCU_SGL_ELEMENT_PAIR_T *sgl_pair; \
   scic_request = scif_io_request_get_scic_handle((scsi_io)); \
   sgl_pair     = scic_sds_request_get_sgl_element_pair(scic_request, 0); \
 \
   if ((current_sge) == NULL) \
   { \
      *(next_sge) = &(sgl_pair->A); \
   } \
   else \
   { \
      *(next_sge) = &(sgl_pair->B); \
   } \
}

#define sati_cb_sge_write(current_sge, phys_address_low, phys_address_high, byte_length) \
{ \
   SCU_SGL_ELEMENT_T * scu_sge = (SCU_SGL_ELEMENT_T*) (current_sge); \
   scu_sge->address_upper      = (phys_address_high); \
   scu_sge->address_lower      = (phys_address_low); \
   scu_sge->length             = (byte_length); \
   scu_sge->address_modifier   = 0; \
}

#define sati_cb_do_translate_response(request) \
    (request)->stp.sequence.is_translate_response_required

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIF_SAS_SATI_BINDING_H_

