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
* @brief This file contains the method implementations to translate
*        SCSI Write Buffer command based of the SAT2v07 spec.
*/

#include <dev/isci/scil/sati_write_buffer.h>
#include <dev/isci/scil/sati_callbacks.h>
#include <dev/isci/scil/sati_util.h>

#define WRITE_BUFFER_WRITE_DATA              0x02
#define WRITE_BUFFER_DOWNLOAD_SAVE           0x05
#define WRITE_BUFFER_OFFSET_DOWNLOAD_SAVE    0x07
#define DOWNLOAD_MICROCODE_BLOCK_SIZE        512

/**
* @brief This method will translate the SCSI Write Buffer command
*        into a corresponding ATA Write Buffer and Download Microcode commands.
*        For more information on the parameters passed to this method,
*        please reference sati_translate_command().
*
* @return Indicates if the command translation succeeded.
* @retval SATI_SUCCESS indicates that the translation was supported and occurred
*         without error.
* @retval SATI_FAILURE_CHECK_RESPONSE_DATA This value is returned if
*         there is a translation failure.
*/
SATI_STATUS sati_write_buffer_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   U8 * cdb = sati_cb_get_cdb_address(scsi_io);
   SATI_STATUS status = SATI_FAILURE;
   U32 allocation_length;
   U32 allocation_blocks;
   U32 buffer_offset;

   allocation_length = ((sati_get_cdb_byte(cdb, 6) << 16) |
                        (sati_get_cdb_byte(cdb, 7) << 8)  |
                        (sati_get_cdb_byte(cdb, 8)));

   buffer_offset = ((sati_get_cdb_byte(cdb, 3) << 16) |
                    (sati_get_cdb_byte(cdb, 4) << 8)  |
                    (sati_get_cdb_byte(cdb, 5)));

   sequence->allocation_length = allocation_length;
   allocation_blocks = allocation_length / DOWNLOAD_MICROCODE_BLOCK_SIZE;

   switch(sati_get_cdb_byte(cdb, 1))
   {
      case WRITE_BUFFER_WRITE_DATA:
         if((allocation_length == DOWNLOAD_MICROCODE_BLOCK_SIZE) && 
            (buffer_offset == 0) &&
            (sati_get_cdb_byte(cdb, 2) == 0))
         {
            sati_ata_write_buffer_construct(ata_io, sequence);
            sequence->type = SATI_SEQUENCE_WRITE_BUFFER;
            sequence->state = SATI_SEQUENCE_STATE_AWAIT_RESPONSE;
            status = SATI_SUCCESS;
         }
         else
         {
            sati_scsi_sense_data_construct(
               sequence,
               scsi_io,
               SCSI_STATUS_CHECK_CONDITION,
               SCSI_SENSE_ILLEGAL_REQUEST,
               SCSI_ASC_INVALID_FIELD_IN_CDB,
               SCSI_ASCQ_INVALID_FIELD_IN_CDB
            );

            sequence->state = SATI_SEQUENCE_STATE_FINAL;
            status = SATI_FAILURE_CHECK_RESPONSE_DATA;
         }
      break;

      case WRITE_BUFFER_DOWNLOAD_SAVE:

         sati_ata_download_microcode_construct(
            ata_io,
            sequence,
            ATA_MICROCODE_DOWNLOAD_SAVE,
            allocation_length,
            buffer_offset
         );

         sequence->type = SATI_SEQUENCE_WRITE_BUFFER_MICROCODE;
         sequence->state = SATI_SEQUENCE_STATE_AWAIT_RESPONSE;
         status = SATI_SUCCESS;
      break;

      case WRITE_BUFFER_OFFSET_DOWNLOAD_SAVE:
         if(((allocation_length & 0x000001FF) == 0) && //Bits 08:00 need to be zero per SAT2v7
            ((buffer_offset & 0x000001FF) == 0)     &&
            (allocation_blocks <= sequence->device->max_blocks_per_microcode_command) &&
            ((allocation_blocks >= sequence->device->min_blocks_per_microcode_command) ||
            (allocation_length == 0)))
         {
            sati_ata_download_microcode_construct(
               ata_io,
               sequence,
               ATA_MICROCODE_OFFSET_DOWNLOAD,
               allocation_length,
               buffer_offset
            );

            sequence->type = SATI_SEQUENCE_WRITE_BUFFER_MICROCODE;
            sequence->state = SATI_SEQUENCE_STATE_AWAIT_RESPONSE;
            status = SATI_SUCCESS;
         }
         else
         {
            sati_scsi_sense_data_construct(
               sequence,
               scsi_io,
               SCSI_STATUS_CHECK_CONDITION,
               SCSI_SENSE_ILLEGAL_REQUEST,
               SCSI_ASC_INVALID_FIELD_IN_CDB,
               SCSI_ASCQ_INVALID_FIELD_IN_CDB
            );

            sequence->state = SATI_SEQUENCE_STATE_FINAL;
            status = SATI_FAILURE_CHECK_RESPONSE_DATA;
         }
      break;

      default: //unsupported Write Buffer Mode
         sati_scsi_sense_data_construct(
            sequence,
            scsi_io,
            SCSI_STATUS_CHECK_CONDITION,
            SCSI_SENSE_ILLEGAL_REQUEST,
            SCSI_ASC_INVALID_FIELD_IN_CDB,
            SCSI_ASCQ_INVALID_FIELD_IN_CDB
         );

         sequence->state = SATI_SEQUENCE_STATE_FINAL;
         status = SATI_FAILURE_CHECK_RESPONSE_DATA;
      break;
   }
   return status;
}

/**
* @brief This method will complete the Write Buffer Translation by checking
*        for ATA errors and then creating a unit attention condition for
*        changed microcode.
*
* @return Indicates if the command translation succeeded.
* @retval SATI_FAILURE_CHECK_RESPONSE_DATA This value is returned if
*         there is a translation failure.
* @retval SATI_COMPLETE indicates that the translation was supported, occurred without
*         error, and no additional translation is necessary.
*/
SATI_STATUS sati_write_buffer_translate_response(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   U8 * register_fis = sati_cb_get_d2h_register_fis_address(ata_io);
   U8 ata_status = (U8) sati_get_ata_status(register_fis);
   SATI_STATUS status = SATI_FAILURE;

   if (ata_status & ATA_STATUS_REG_ERROR_BIT)
   {
      sati_scsi_sense_data_construct(
         sequence,
         scsi_io,
         SCSI_STATUS_CHECK_CONDITION,
         SCSI_SENSE_ABORTED_COMMAND,
         SCSI_ASC_NO_ADDITIONAL_SENSE,
         SCSI_ASCQ_NO_ADDITIONAL_SENSE
      );
      status = SATI_FAILURE_CHECK_RESPONSE_DATA;
   }
   else
   {
      switch(sequence->type)
      {
         case SATI_SEQUENCE_WRITE_BUFFER_MICROCODE:
            sati_scsi_sense_data_construct(
               sequence,
               scsi_io,
               SCSI_STATUS_GOOD,
               SCSI_SENSE_UNIT_ATTENTION,
               SCSI_ASC_MICROCODE_HAS_CHANGED,
               SCSI_ASCQ_MICROCODE_HAS_CHANGED
            );
            status = SATI_COMPLETE;
         break;

         default:
            status = SATI_COMPLETE;
         break;
      }
   }

   sequence->state = SATI_SEQUENCE_STATE_FINAL;
   return status;
}
