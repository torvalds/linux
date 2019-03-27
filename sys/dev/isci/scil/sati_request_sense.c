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
 *        SCSI Request Sense command based of the SAT spec.
 */

#if !defined(DISABLE_SATI_REQUEST_SENSE)

#include <dev/isci/scil/sati_request_sense.h>
#include <dev/isci/scil/sati_device.h>
#include <dev/isci/scil/sati_util.h>
#include <dev/isci/scil/intel_scsi.h>
#include <dev/isci/scil/intel_ata.h>
#include <dev/isci/scil/intel_sat.h>
#include <dev/isci/scil/sati_callbacks.h>
#include <dev/isci/scil/sati_mode_pages.h>

#define MRIE_BYTE 3
#define DEXCPT_BYTE 2

/**
 * @brief This method will translate the SCSI request sense command
 *        into corresponding ATA commands.  Depending on supported and enabled
 *        capabilities like SMART, different ATA commands can be selected.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @return Indicates if the command translation succeeded.
 * @retval SATI_SUCCESS indicates that the translation was supported and occurred
 *         without error.
 * @retval SATI_FAILURE_CHECK_RESPONSE_DATA This value is returned if
 *         the SATII is processing a format unit commmand.
 * @retval SATI_COMPLETE indicates that the translation was supported, occurred without
 *         error, and no additional translation is necessary.
 */
SATI_STATUS sati_request_sense_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   U8 * cdb = sati_cb_get_cdb_address(scsi_io);

   //check if SATI is processing format unit command
   switch(sequence->device->state)
   {
      case SATI_DEVICE_STATE_FORMAT_UNIT_IN_PROGRESS:
         sati_scsi_sense_data_construct(
            sequence,
            scsi_io,
            SCSI_STATUS_GOOD,
            SCSI_SENSE_NOT_READY,
            SCSI_ASC_LUN_FORMAT_IN_PROGRESS,
            SCSI_ASCQ_LUN_FORMAT_IN_PROGRESS
         );
         return SATI_COMPLETE;
      break;

      case SATI_DEVICE_STATE_UNIT_ATTENTION_CONDITION:
         sati_scsi_sense_data_construct(
            sequence,
            scsi_io,
            SCSI_STATUS_GOOD,
            SCSI_SENSE_UNIT_ATTENTION,
            sequence->device->unit_attention_asc,
            sequence->device->unit_attention_ascq
         );
         return SATI_COMPLETE;
      break;
      //sending sense data status Idle, this is set by start_stop_unit
      case SATI_DEVICE_STATE_IDLE:
         sati_scsi_sense_data_construct(
            sequence,
            scsi_io,
            SCSI_STATUS_GOOD,
            SCSI_SENSE_NO_SENSE,
            SCSI_ASC_POWER_STATE_CHANGE,
            SCSI_ASCQ_IDLE_CONDITION_ACTIVATE_BY_COMMAND
         );
         return SATI_COMPLETE;
      break;
      //sending sense data status Standby, this is set by start_stop_unit
      case SATI_DEVICE_STATE_STANDBY:
         sati_scsi_sense_data_construct(
            sequence,
            scsi_io,
            SCSI_STATUS_GOOD,
            SCSI_SENSE_NO_SENSE,
            SCSI_ASC_POWER_STATE_CHANGE,
            SCSI_ASCQ_STANDBY_CONDITION_ACTIVATE_BY_COMMAND
         );
         return SATI_COMPLETE;
      break;

      case SATI_DEVICE_STATE_STOPPED:
         sati_scsi_sense_data_construct(
            sequence,
            scsi_io,
            SCSI_STATUS_GOOD,
            SCSI_SENSE_NO_SENSE,
            SCSI_ASC_NO_ADDITIONAL_SENSE,
            SCSI_ASCQ_NO_ADDITIONAL_SENSE
         );
         return SATI_COMPLETE;
      break;

      default:
      break;
   }

   sequence->allocation_length = sati_get_cdb_byte(cdb, 4);

   //Check if the device has SMART support & SMART enabled
   if(sequence->device->capabilities & SATI_DEVICE_CAP_SMART_SUPPORT)
   {
       if(sequence->device->capabilities & SATI_DEVICE_CAP_SMART_ENABLE)
       {
            sati_ata_smart_return_status_construct(
                           ata_io,
                           sequence,
                           ATA_SMART_SUB_CMD_RETURN_STATUS
            );

            sequence->type = SATI_SEQUENCE_REQUEST_SENSE_SMART_RETURN_STATUS;
            return SATI_SUCCESS;
        }
   }
   sati_ata_check_power_mode_construct(ata_io, sequence);
   sequence->type = SATI_SEQUENCE_REQUEST_SENSE_CHECK_POWER_MODE;
   return SATI_SUCCESS;
}

/**
 * @brief This method will translate the response to the SATI Request Sense
 *        translation. ATA_Check_Power_Mode and ATA_SMART_Return_Status will
 *        be translated into a SCSI sense data response.
 *
 * @return SATI_STATUS Indicates if the response translation succeeded.
 *
 */
SATI_STATUS sati_request_sense_translate_response(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   U8 * register_fis = sati_cb_get_d2h_register_fis_address(ata_io);
   U32 mid_register;
   U32 high_register;
   U32 sector_count;
   SATI_STATUS status = SATI_FAILURE;

   if(sati_get_ata_status(register_fis) & ATA_STATUS_REG_ERROR_BIT)
   {
      sati_scsi_sense_data_construct(
         sequence,
         scsi_io,
         SCSI_STATUS_CHECK_CONDITION,
         SCSI_SENSE_ABORTED_COMMAND,
         SCSI_ASC_NO_ADDITIONAL_SENSE ,
         SCSI_ASCQ_NO_ADDITIONAL_SENSE
      );
      status = SATI_FAILURE_CHECK_RESPONSE_DATA;
   }
   else
   {
      switch(sequence->type)
      {
         case SATI_SEQUENCE_REQUEST_SENSE_SMART_RETURN_STATUS:

            mid_register = sati_get_ata_lba_mid(register_fis);
            high_register = sati_get_ata_lba_high(register_fis);
            if(mid_register == ATA_MID_REGISTER_THRESHOLD_EXCEEDED
               && high_register == ATA_HIGH_REGISTER_THRESHOLD_EXCEEDED)
            {
               sati_request_sense_data_response_construct(
                  sequence,
                  scsi_io,
                  SCSI_SENSE_NO_SENSE,
                  SCSI_ASC_HARDWARE_IMPENDING_FAILURE,
                  SCSI_ASCQ_GENERAL_HARD_DRIVE_FAILURE
               );
               status = SATI_COMPLETE;
            }
            else
            {
               sati_request_sense_data_response_construct(
                  sequence,
                  scsi_io,
                  SCSI_SENSE_NO_SENSE,
                  SCSI_ASC_NO_ADDITIONAL_SENSE,
                  SCSI_ASCQ_NO_ADDITIONAL_SENSE
               );
               status = SATI_COMPLETE;
            }
         break;

         case SATI_SEQUENCE_REQUEST_SENSE_CHECK_POWER_MODE:

            sector_count = sati_get_ata_sector_count(register_fis);

            switch(sector_count)
            {
                case ATA_STANDBY_POWER_MODE:
                   sati_request_sense_data_response_construct(
                      sequence,
                      scsi_io,
                      SCSI_SENSE_NO_SENSE,
                      SCSI_ASC_POWER_STATE_CHANGE,
                      SCSI_ASCQ_POWER_STATE_CHANGE_TO_STANDBY
                   );
                   status = SATI_COMPLETE;
                break;

                case ATA_IDLE_POWER_MODE:
                   sati_request_sense_data_response_construct(
                      sequence,
                      scsi_io,
                      SCSI_SENSE_NO_SENSE,
                      SCSI_ASC_POWER_STATE_CHANGE,
                      SCSI_ASCQ_POWER_STATE_CHANGE_TO_IDLE
                   );
                   status = SATI_COMPLETE;
                break;

                case ATA_ACTIVE_POWER_MODE:
                   sati_request_sense_data_response_construct(
                      sequence,
                      scsi_io,
                      SCSI_SENSE_NO_SENSE,
                      SCSI_ASC_NO_ADDITIONAL_SENSE,
                      SCSI_ASCQ_NO_ADDITIONAL_SENSE
                   );
                   status = SATI_COMPLETE;
                break;

                default:
                break;
             }
         break;

         default:
            sati_request_sense_data_response_construct(
               sequence,
               scsi_io,
               SCSI_SENSE_NO_SENSE,
               SCSI_ASC_NO_ADDITIONAL_SENSE,
               SCSI_ASCQ_NO_ADDITIONAL_SENSE
            );
            status = SATI_COMPLETE;
      }
   }

   return status;
}

/**
 * @brief This method will construct a response for the sati_request_sense
 *        translation. The response will be returned in the data buffer instead
 *        of the response buffer, using sense data format described in SPC-4.
 *
 */
void sati_request_sense_data_response_construct(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   U8                           sense_key,
   U8                           additional_sense_code,
   U8                           additional_sense_code_qualifier
)
{
   sati_set_data_byte(
      sequence,
      scsi_io,
      0,
      SCSI_FIXED_CURRENT_RESPONSE_CODE | SCSI_FIXED_SENSE_DATA_VALID_BIT
   );

   sati_set_data_byte(sequence, scsi_io, 1, 0);
   sati_set_data_byte(sequence, scsi_io, 2, sense_key);
   sati_set_data_byte(sequence, scsi_io, 3, 0);
   sati_set_data_byte(sequence, scsi_io, 4, 0);
   sati_set_data_byte(sequence, scsi_io, 5, 0);
   sati_set_data_byte(sequence, scsi_io, 6, 0);
   sati_set_data_byte(sequence, scsi_io, 7, 0);
   sati_set_data_byte(sequence, scsi_io, 8, 0);
   sati_set_data_byte(sequence, scsi_io, 9, 0);
   sati_set_data_byte(sequence, scsi_io, 10, 0);
   sati_set_data_byte(sequence, scsi_io, 11, 0);
   sati_set_data_byte(sequence, scsi_io, 12, additional_sense_code);
   sati_set_data_byte(sequence, scsi_io, 13, additional_sense_code_qualifier);
   sati_set_data_byte(sequence, scsi_io, 14, 0);
   sati_set_data_byte(sequence, scsi_io, 15, 0);
   sati_set_data_byte(sequence, scsi_io, 16, 0);
   sati_set_data_byte(sequence, scsi_io, 17, 0);
}

#endif // !defined(DISABLE_SATI_REQUEST_SENSE)

