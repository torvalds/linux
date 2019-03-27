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
 * @brief This file contains all of the method implementations that
 *        can be utilized by a user to perform SCSI-to-ATA Translation.
 *        SATI adheres to the www.t10.org SAT specification.
 *
 * For situations where compliance is not observed, the SATI will
 * return an error indication (most likely INVALID FIELD IN CDB sense data).
 */

#include <dev/isci/scil/sati.h>
#include <dev/isci/scil/sati_util.h>
#include <dev/isci/scil/sati_atapi.h>
#include <dev/isci/scil/sati_callbacks.h>
#include <dev/isci/scil/intel_ata.h>
#include <dev/isci/scil/intel_scsi.h>
#include <dev/isci/scil/intel_sat.h>
#include <dev/isci/scil/sati_report_luns.h>


//******************************************************************************
//* P U B L I C   M E T H O D S
//******************************************************************************

SATI_STATUS sati_atapi_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   SATI_DEVICE_T              * sati_device,
   void                       * scsi_io,
   void                       * atapi_io
)
{
   SATI_STATUS   status;
   U8          * cdb = sati_cb_get_cdb_address(scsi_io);

   SATA_FIS_REG_H2D_T * register_fis =
      (SATA_FIS_REG_H2D_T *)sati_cb_get_h2d_register_fis_address(atapi_io);

   U8 io_direction = SATI_DATA_DIRECTION_IN;

   //No sense response has been set for the translation sequence yet
   sequence->is_sense_response_set = FALSE;
   // Default to no translation response required
   sequence->is_translate_response_required = FALSE;

   sequence->number_data_bytes_set = 0;
   sequence->device  = sati_device;
   sequence->command_specific_data.scratch = 0;

   sati_cb_get_data_direction(scsi_io, &io_direction);

   //set sat protocol.
   if (io_direction == SATI_DATA_DIRECTION_NONE)
      sequence->protocol = SAT_PROTOCOL_PACKET_NON_DATA;
   else if (io_direction == SATI_DATA_DIRECTION_IN)
      sequence->protocol = SAT_PROTOCOL_PACKET_DMA_DATA_IN;
   else if (io_direction == SATI_DATA_DIRECTION_OUT)
      sequence->protocol = SAT_PROTOCOL_PACKET_DMA_DATA_OUT;

   // We don't send Report Luns command out.
   if  (sati_get_cdb_byte(cdb, 0) == SCSI_REPORT_LUNS)
   {
      status = sati_report_luns_translate_command(
                  sequence, scsi_io, atapi_io
               );
   }
   else if (sati_cb_get_lun(scsi_io) != 0)
   {
      sati_scsi_sense_data_construct(
         sequence,
         scsi_io,
         SCSI_STATUS_CHECK_CONDITION,
         SCSI_SENSE_ILLEGAL_REQUEST,
         SCSI_ASC_LOGICAL_UNIT_NOT_SUPPORTED,
         0
      );
      status = SATI_FAILURE_CHECK_RESPONSE_DATA;
   }
   else
   {
      if (sequence->state == SATI_SEQUENCE_STATE_INCOMPLETE)
      {  //Request Sense command is required.
         U8 request_sense_cdb[SATI_ATAPI_REQUEST_SENSE_CDB_LENGTH] =
            {0x3, 0x0, 0x0, 0x0, 0x12, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};

         //set the sequence->protocol to DATA_IN anyway;
         sequence->protocol = SAT_PROTOCOL_PACKET_DMA_DATA_IN;

         //set the cdb for Request Sense using command_specific_data field.
         memcpy(sequence->command_specific_data.sati_atapi_data.request_sense_cdb,
                request_sense_cdb,
                SATI_ATAPI_REQUEST_SENSE_CDB_LENGTH
               );
      }

      //build Packet Fis for any other command translation.
      register_fis->command = ATA_PACKET;
      register_fis->features |= ATA_PACKET_FEATURE_DMA;

      register_fis->fis_type = SATA_FIS_TYPE_REGH2D;
      register_fis->command_flag = 1;

      status = SATI_SUCCESS;
   }

   return status;
}


SATI_STATUS sati_atapi_translate_command_response(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * atapi_io
)
{
   SATI_STATUS   status       = SATI_COMPLETE;
   U8          * register_fis = sati_cb_get_d2h_register_fis_address(atapi_io);
   U8            ata_status;

   /**
    * If the device fault bit is set in the status register, then
    * set the sense data and return.
    */
   ata_status = (U8) sati_get_ata_status(register_fis);
   if (ata_status & ATA_STATUS_REG_DEVICE_FAULT_BIT)
   {
      sati_scsi_sense_data_construct(
         sequence,
         scsi_io,
         SCSI_STATUS_CHECK_CONDITION,
         SCSI_SENSE_HARDWARE_ERROR,
         SCSI_ASC_INTERNAL_TARGET_FAILURE,
         SCSI_ASCQ_INTERNAL_TARGET_FAILURE
      );

      sequence->device->state = SATI_DEVICE_STATE_DEVICE_FAULT_OCCURRED;

      return SATI_FAILURE_CHECK_RESPONSE_DATA;
   }
   else if (ata_status & ATA_STATUS_REG_ERROR_BIT)
   {
       //reset the register_fis.
       memset(register_fis, 0, sizeof(SATA_FIS_REG_D2H_T));

       //Internal Request Sense command is needed.
       sequence->state = SATI_SEQUENCE_STATE_INCOMPLETE;
       return SATI_SEQUENCE_INCOMPLETE;
   }

   return status;
}

void sati_atapi_translate_request_sense_response(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * atapi_io
)
{
   //sense data is already in place.
   SCI_SSP_RESPONSE_IU_T * rsp_iu = (SCI_SSP_RESPONSE_IU_T*)
                                 sati_cb_get_response_iu_address(scsi_io);

   sati_scsi_common_response_iu_construct(
      rsp_iu,
      SCSI_STATUS_CHECK_CONDITION,
      sati_scsi_get_sense_data_length(sequence, scsi_io),
      SCSI_RESPONSE_DATA_PRES_SENSE_DATA
   );

   sequence->is_sense_response_set = TRUE;

   sequence->state = SATI_SEQUENCE_STATE_FINAL;
}


U32 sati_atapi_translate_number_of_bytes_transferred(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * atapi_io
)
{
   U8* cdb = sati_cb_get_cdb_address(scsi_io);
   U8 response_data;
   U32 data_length = 0;

   switch(cdb[0])
   {
      case SCSI_MODE_SENSE_10:
         sati_cb_get_data_byte(scsi_io, 1, &response_data);
         data_length = response_data+2;
         break;

      case 0x51: //READ DISC INFORMATION
         sati_cb_get_data_byte(scsi_io, 1, &response_data);
         data_length = response_data+2;
         break;

      default:
         break;
   }

   return data_length;
}
