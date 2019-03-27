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
* @brief This file contains the implementation to translate
*        SCSI Read Buffer command based on the SAT spec.
*/

#if !defined(DISABLE_SATI_READ_BUFFER)

#include <dev/isci/scil/sati_read_buffer.h>
#include <dev/isci/scil/sati_callbacks.h>
#include <dev/isci/scil/sati_util.h>


/**
* @brief This method will translate the SCSI Read Buffer command
*        into a corresponding ATA Read Buffer command.
*        For more information on the parameters passed to this method,
*        please reference sati_translate_command().
*
* @return Indicates if the command translation succeeded.
* @retval SATI_SUCCESS indicates that the translation was supported and occurred
*         without error.
* @retval SATI_FAILURE_CHECK_RESPONSE_DATA This value is returned if
*         there is a translation failure.
* @retval SATI_COMPLETE indicates that the translation was supported, occurred without
*         error, and no additional translation is necessary.
*/
SATI_STATUS sati_read_buffer_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   U8 * cdb = sati_cb_get_cdb_address(scsi_io);
   SATI_STATUS status = SATI_FAILURE;
   U32 allocation_length;
   U32 buffer_offset;

   allocation_length = ((sati_get_cdb_byte(cdb, 6) << 16) |
                        (sati_get_cdb_byte(cdb, 7) << 8)  |
                        (sati_get_cdb_byte(cdb, 8)));

   buffer_offset = ((sati_get_cdb_byte(cdb, 3) << 16) |
                    (sati_get_cdb_byte(cdb, 4) << 8)  |
                    (sati_get_cdb_byte(cdb, 5)));

   sequence->allocation_length = allocation_length;

   switch(sati_get_cdb_byte(cdb, 1))
   {
      case SATI_READ_BUFFER_MODE_DATA:
         if((allocation_length == 512) && (buffer_offset == 0) &&
            (sati_get_cdb_byte(cdb, 2) == 0))
         {
            sati_ata_read_buffer_construct(ata_io, sequence);
            sequence->type = SATI_SEQUENCE_READ_BUFFER;
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

      case SATI_READ_BUFFER_MODE_DESCRIPTOR:

         //allocation legnth must be at least four to return translated data
         if(allocation_length < 4)
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
         else
         {
            //figure out if we support other buffer id's
            sati_set_data_byte(sequence, scsi_io, 0, 0x09); //offset boundary
            sati_set_data_byte(sequence, scsi_io, 1, 0x00);
            sati_set_data_byte(sequence, scsi_io, 2, 0x02); //buffer capacity 0x200
            sati_set_data_byte(sequence, scsi_io, 3, 0x00);
            sequence->state = SATI_SEQUENCE_STATE_FINAL;
            status = SATI_COMPLETE;
         }
      break;

      default:
         //Unspecified sat2v7, returning invalid cdb
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
   return status;
}

/**
* @brief This method will complete the Read Buffer Translation by copying the
         ATA response data into the SCSI request DATA buffer.
*        For more information on the parameters passed to this method,
*        please reference sati_translate_command().
*
* @return Indicates if the command translation succeeded.
* @retval SATI_FAILURE_CHECK_RESPONSE_DATA This value is returned if
*         there is a translation failure.
* @retval SATI_COMPLETE indicates that the translation was supported, occurred without
*         error, and no additional translation is necessary.
*/
SATI_STATUS sati_read_buffer_translate_response(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   U8 * register_fis = sati_cb_get_d2h_register_fis_address(ata_io);
   U8   ata_status = (U8) sati_get_ata_status(register_fis);
   SATI_STATUS status = SATI_COMPLETE;

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
      void * ata_data = sati_cb_get_ata_data_address(ata_io);

      if(ata_data == NULL)
      {
         status = SATI_FAILURE;
      }
      else
      {
         //copy ATA data into SCSI data buffer
         sati_copy_data(
            sequence,
            scsi_io,
            0,
            ata_data,
            512
         );
      }
   }

   sequence->state = SATI_SEQUENCE_STATE_FINAL;
   return status;
}


#endif //!defined(DISABLE_SATI_READ_BUFFER)
