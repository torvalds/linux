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
 * @brief This file contains the protected interface structures, constants,
 *        and methods for the SCIF_SAS_STP_REMOTE_DEVICE object.
 */

#include <dev/isci/scil/scif_sas_stp_remote_device.h>
#include <dev/isci/scil/scif_sas_remote_device.h>
#include <dev/isci/scil/scif_sas_domain.h>
#include <dev/isci/scil/scif_sas_controller.h>
#include <dev/isci/scil/scif_sas_logger.h>
#include <dev/isci/scil/intel_sat.h>

/**
 * @brief This method performs SATA/STP specific construction of the
 *        STP remote device object.
 *
 * @param[in] device This parameter specifies the STP remote device
 *            object to be constructed.
 *
 * @return none
 */
void scif_sas_stp_remote_device_construct(
   SCIF_SAS_REMOTE_DEVICE_T * device
)
{
   sati_device_construct(
      &device->protocol_device.stp_device.sati_device,
      device->domain->controller->user_parameters.sas.is_sata_ncq_enabled,
      (U8) device->domain->controller->user_parameters.sas.max_ncq_depth,
      device->domain->controller->user_parameters.sas.ignore_fua
   );

   device->protocol_device.stp_device.s_active = 0;
}

/**
 * @brief This method attempts to allocate a valid NCQ tag from the list
 *        of available tags in the remote device.
 *
 * @todo Attempt to find a CLZ like instruction to optimize this routine
 *       down into a few instructions.  I know there is one like it for IA.
 *
 * @param[in] fw_device This parameter specifies the remote device
 *            for which to allocate an available NCQ tag.
 *
 * @return Return an available NCQ tag.
 * @retval 0-31 These values indicate an available tag was successfully
 *         allocated.
 * @return SCIF_SAS_STP_INVALID_NCQ_TAG This value indicates that there are
 *         no available NCQ tags.
 */
U8 scif_sas_stp_remote_device_allocate_ncq_tag(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device
)
{
   U8  ncq_tag  = 0;
   U32 tag_mask = 1;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_sas_stp_remote_device_allocate_ncq_tag(0x%x)\n",
      fw_device
   ));

   // Try to find an unused NCQ tag.
   while (  (fw_device->protocol_device.stp_device.s_active & tag_mask)
         && (ncq_tag < fw_device->protocol_device.stp_device.sati_device.ncq_depth) )
   {
      tag_mask <<= 1;
      ncq_tag++;
   }

   // Check to see if we were able to find an available NCQ tag.
   if (ncq_tag < fw_device->protocol_device.stp_device.sati_device.ncq_depth)
   {
      SCIF_LOG_INFO((
         sci_base_object_get_logger(fw_device),
         SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_IO_REQUEST,
         "RemoteDevice:0x%x NcqTag:0x%x successful NCQ TAG allocation\n",
         fw_device, ncq_tag
      ));

      fw_device->protocol_device.stp_device.s_active |= tag_mask;
      return ncq_tag;
   }

   SCIF_LOG_INFO((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_IO_REQUEST,
      "RemoteDevice:0x%x unable to allocate NCQ TAG\n",
      fw_device
   ));

   // All NCQ tags are in use.
   return SCIF_SAS_INVALID_NCQ_TAG;
}

/**
 * @brief This method removes the specified tag from the list of
 *        outstanding tags.  It doesn't return any values.
 *
 * @param[in] fw_device This parameter specifies the remote device for
 *            which to free an NCQ tag.
 * @param[in] ncq_tag This parameter specifies the NCQ tag that is
 *            to be freed.
 *
 * @return none
 */
void scif_sas_stp_remote_device_free_ncq_tag(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device,
   U8                              ncq_tag
)
{
   SCIF_LOG_INFO((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_IO_REQUEST,
      "RemoteDevice:0x%x NcqTag:0x%x freeing NCQ TAG\n",
      fw_device, ncq_tag
   ));

   fw_device->protocol_device.stp_device.s_active &= ~(1 << ncq_tag);
}

struct SCIF_SAS_REQUEST *
scif_sas_stp_remote_device_get_request_by_ncq_tag(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device,
   U8                              ncq_tag
)
{
   SCIF_SAS_DOMAIN_T                * fw_domain = fw_device->domain;
   SCI_FAST_LIST_ELEMENT_T          * pending_request_element;
   SCIF_SAS_REQUEST_T               * pending_request = NULL;
   SCIF_SAS_REQUEST_T               * matching_request = NULL;

   pending_request_element = fw_domain->request_list.list_head;

   while (pending_request_element != NULL)
   {
      pending_request =
         (SCIF_SAS_REQUEST_T*) sci_fast_list_get_object(pending_request_element);

      // The current element may be deleted from the list because of
      // IO completion so advance to the next element early
      pending_request_element = sci_fast_list_get_next(pending_request_element);

      if (
            (pending_request->device == fw_device) &&
            (pending_request->stp.sequence.protocol == SAT_PROTOCOL_FPDMA) &&
            (pending_request->stp.ncq_tag == ncq_tag)
         )
      {
         matching_request = pending_request;
      }
   }

   return matching_request;
}
