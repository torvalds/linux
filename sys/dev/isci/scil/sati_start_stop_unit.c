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
 * @brief This file contains the method implementations required to
 *        translate the SCSI start stop unit command.
 */

#if !defined(DISABLE_SATI_START_STOP_UNIT)

#include <dev/isci/scil/sati_start_stop_unit.h>
#include <dev/isci/scil/sati_util.h>
#include <dev/isci/scil/sati_callbacks.h>
#include <dev/isci/scil/intel_ata.h>
#include <dev/isci/scil/intel_scsi.h>

/**
 * @brief This method will translate the start stop unit SCSI command into
 *        various ATA commands depends on the value in POWER CONTIDTION, LOEJ
 *        and START fields.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @return Indicate if the command translation succeeded.
 * @retval SCI_SUCCESS This is returned if the command translation was
 *         successful.
 * @retval SATI_FAILURE_CHECK_RESPONSE_DATA Please refer to spec.
 *
 */
SATI_STATUS sati_start_stop_unit_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   U8 * cdb = sati_cb_get_cdb_address(scsi_io);

   switch ( SATI_START_STOP_UNIT_POWER_CONDITION(cdb) )
   {
      case SCSI_START_STOP_UNIT_POWER_CONDITION_START_VALID:
         if ( SATI_START_STOP_UNIT_START_BIT(cdb) == 0
             && SATI_START_STOP_UNIT_LOEJ_BIT(cdb) == 0 )
         {
            if ( SATI_START_STOP_UNIT_NO_FLUSH_BIT(cdb) == 1 )
            {
               //directly send ATA STANDBY_IMMEDIATE
               sati_ata_standby_immediate_construct(ata_io, sequence);
               sequence->command_specific_data.translated_command = ATA_STANDBY_IMMED;
            }
            else
            {
               if ( sequence->state != SATI_SEQUENCE_STATE_INCOMPLETE )
               {
                  //First, send ATA flush command.
                  sati_ata_flush_cache_construct(ata_io, sequence);
                  sequence->command_specific_data.translated_command = ATA_FLUSH_CACHE;

                  //remember there is next step.
                  sequence->state = SATI_SEQUENCE_STATE_INCOMPLETE;
               }
               else
               {
                  //the first step, flush cache command, has completed.
                  //Send standby immediate now.
                  sati_ata_standby_immediate_construct(ata_io, sequence);
                  sequence->command_specific_data.translated_command = ATA_STANDBY_IMMED;

               }
            }
         }
         else if ( SATI_START_STOP_UNIT_START_BIT(cdb) == 0
                  && SATI_START_STOP_UNIT_LOEJ_BIT(cdb) == 1 )
         {
            //need to know whether the device supports removable medial feature set.
            if (sequence->device->capabilities & SATI_DEVICE_CAP_REMOVABLE_MEDIA)
            {
               //send ATA MEDIA EJECT command.
               sati_ata_media_eject_construct(ata_io, sequence);
               sequence->command_specific_data.translated_command = ATA_MEDIA_EJECT;
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
               return SATI_FAILURE_CHECK_RESPONSE_DATA;
            }
         }
         else if ( SATI_START_STOP_UNIT_START_BIT(cdb) == 1
                  && SATI_START_STOP_UNIT_LOEJ_BIT(cdb) == 0 )
         {
            //send an ATA verify command
            sati_ata_read_verify_sectors_construct(ata_io, sequence);
            sequence->command_specific_data.translated_command = ATA_READ_VERIFY_SECTORS;
         }
         else if ( SATI_START_STOP_UNIT_START_BIT(cdb) == 1
                  && SATI_START_STOP_UNIT_LOEJ_BIT(cdb) == 1 )
         {
            sati_scsi_sense_data_construct(
               sequence,
               scsi_io,
               SCSI_STATUS_CHECK_CONDITION,
               SCSI_SENSE_ILLEGAL_REQUEST,
               SCSI_ASC_INVALID_FIELD_IN_CDB,
               SCSI_ASCQ_INVALID_FIELD_IN_CDB
            );
            return SATI_FAILURE_CHECK_RESPONSE_DATA;
         }

         break;
      //Power Condition Field is set to 0x01(Device to transition to Active state)
      case SCSI_START_STOP_UNIT_POWER_CONDITION_ACTIVE:

         if( sequence->state != SATI_SEQUENCE_STATE_INCOMPLETE )
         {
            sati_ata_idle_construct(ata_io, sequence);
            sequence->state = SATI_SEQUENCE_STATE_INCOMPLETE;
            sequence->command_specific_data.translated_command = ATA_IDLE;
         }
         else
         {
            sati_ata_read_verify_sectors_construct(ata_io, sequence);
            sequence->command_specific_data.translated_command = ATA_READ_VERIFY_SECTORS;
         }
         break;

      //Power Condition Field is set to 0x02(Device to transition to Idle state)
      case SCSI_START_STOP_UNIT_POWER_CONDITION_IDLE:

         if( SATI_START_STOP_UNIT_NO_FLUSH_BIT(cdb) == 0 &&
             sequence->state != SATI_SEQUENCE_STATE_INCOMPLETE )
         {
            sati_ata_flush_cache_construct(ata_io, sequence);
            sequence->command_specific_data.translated_command = ATA_FLUSH_CACHE;
            sequence->state = SATI_SEQUENCE_STATE_INCOMPLETE;
         }
         else
         {
            if( SATI_START_STOP_UNIT_POWER_CONDITION_MODIFIER(cdb) == 0 )
            {
               sati_ata_idle_immediate_construct(ata_io, sequence);
            }
            else
            {
               sati_ata_idle_immediate_unload_construct(ata_io, sequence);
            }
            sequence->command_specific_data.translated_command = ATA_IDLE_IMMED;
         }
         break;

      //Power Condition Field is set to 0x03(Device to transition to Standby state)
      case SCSI_START_STOP_UNIT_POWER_CONDITION_STANDBY:
         if( SATI_START_STOP_UNIT_NO_FLUSH_BIT(cdb) == 0 &&
            sequence->state != SATI_SEQUENCE_STATE_INCOMPLETE )
         {
            sati_ata_flush_cache_construct(ata_io, sequence);
            sequence->command_specific_data.translated_command = ATA_FLUSH_CACHE;
            sequence->state = SATI_SEQUENCE_STATE_INCOMPLETE;
         }
         else
         {
            sati_ata_standby_immediate_construct(ata_io, sequence);
            sequence->command_specific_data.translated_command = ATA_STANDBY_IMMED;
         }
         break;

      //Power Condition Field is set to 0xB(force Standby state)
      case SCSI_START_STOP_UNIT_POWER_CONDITION_FORCE_S_CONTROL:

         if( SATI_START_STOP_UNIT_NO_FLUSH_BIT(cdb) == 0 &&
            sequence->state != SATI_SEQUENCE_STATE_INCOMPLETE )
         {
            sati_ata_flush_cache_construct(ata_io, sequence);
            sequence->command_specific_data.translated_command = ATA_FLUSH_CACHE;
            sequence->state = SATI_SEQUENCE_STATE_INCOMPLETE;
         }
         else
         {
            sati_ata_standby_construct(ata_io, sequence, 0);
            sequence->command_specific_data.translated_command = ATA_STANDBY;
         }
         break;

      case SCSI_START_STOP_UNIT_POWER_CONDITION_LU_CONTROL:
      default:  //TBD.
         sati_scsi_sense_data_construct(
            sequence,
            scsi_io,
            SCSI_STATUS_CHECK_CONDITION,
            SCSI_SENSE_ILLEGAL_REQUEST,
            SCSI_ASC_INVALID_FIELD_IN_CDB,
            SCSI_ASCQ_INVALID_FIELD_IN_CDB
         );
         return SATI_FAILURE_CHECK_RESPONSE_DATA;
         break;
   }

   if ( SATI_START_STOP_UNIT_IMMED_BIT(cdb) == 1 )
   {
      //@todo: return good status now.
      ;
   }
   sequence->type = SATI_SEQUENCE_START_STOP_UNIT;
   return SATI_SUCCESS;
}


/**
 * @brief This method will translate the ATA command register FIS
 *        response into an appropriate SCSI response for START STOP UNIT.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_response().
 *
 * @return Indicate if the response translation succeeded.
 * @retval SCI_SUCCESS This is returned if the data translation was
 *         successful.
 */
SATI_STATUS sati_start_stop_unit_translate_response(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   U8 * register_fis = sati_cb_get_d2h_register_fis_address(ata_io);
   U8 * cdb = sati_cb_get_cdb_address(scsi_io);

   if (sati_get_ata_status(register_fis) & ATA_STATUS_REG_ERROR_BIT)
   {
      switch ( sequence->command_specific_data.translated_command )
      {
         case ATA_FLUSH_CACHE:
         case ATA_STANDBY_IMMED:
         case ATA_IDLE_IMMED:
         case ATA_IDLE:
         case ATA_STANDBY:
            //Note: There is lack of reference in spec of the error handling for
            //READ_VERIFY command.
         case ATA_READ_VERIFY_SECTORS:
            sati_scsi_sense_data_construct(
               sequence,
               scsi_io,
               SCSI_STATUS_CHECK_CONDITION,
               SCSI_SENSE_ABORTED_COMMAND,
               SCSI_ASC_COMMAND_SEQUENCE_ERROR,
               SCSI_ASCQ_NO_ADDITIONAL_SENSE
            );
            break;

         case ATA_MEDIA_EJECT:
            sati_scsi_sense_data_construct(
               sequence,
               scsi_io,
               SCSI_STATUS_CHECK_CONDITION,
               SCSI_SENSE_ABORTED_COMMAND,
               SCSI_ASC_MEDIA_LOAD_OR_EJECT_FAILED,
               SCSI_ASCQ_NO_ADDITIONAL_SENSE
            );
            break;

         default:
            sati_scsi_sense_data_construct(
               sequence,
               scsi_io,
               SCSI_STATUS_CHECK_CONDITION,
               SCSI_SENSE_ILLEGAL_REQUEST,
               SCSI_ASC_INVALID_FIELD_IN_CDB,
               SCSI_ASCQ_INVALID_FIELD_IN_CDB
            );
            break;
      }
      sequence->state = SATI_SEQUENCE_STATE_FINAL;
      return SATI_FAILURE_CHECK_RESPONSE_DATA;
   }
   else
   {
      switch ( sequence->command_specific_data.translated_command )
      {
         case ATA_READ_VERIFY_SECTORS:

            sati_scsi_sense_data_construct(
               sequence,
               scsi_io,
               SCSI_STATUS_GOOD,
               SCSI_SENSE_NO_SENSE,
               SCSI_ASC_NO_ADDITIONAL_SENSE,
               SCSI_ASCQ_NO_ADDITIONAL_SENSE
            );
            //device state is now operational(active)
            sequence->device->state = SATI_DEVICE_STATE_OPERATIONAL;
            sequence->state = SATI_SEQUENCE_STATE_FINAL;
            break;

         case ATA_IDLE_IMMED:

            sati_scsi_sense_data_construct(
               sequence,
               scsi_io,
               SCSI_STATUS_GOOD,
               SCSI_SENSE_NO_SENSE,
               SCSI_ASC_NO_ADDITIONAL_SENSE,
               SCSI_ASCQ_NO_ADDITIONAL_SENSE
            );
            sequence->device->state = SATI_DEVICE_STATE_IDLE;
            sequence->state = SATI_SEQUENCE_STATE_FINAL;
            break;

         //These three commands will be issued when the power condition is 0x00 or 0x03
         case ATA_MEDIA_EJECT:
         case ATA_STANDBY:
         case ATA_STANDBY_IMMED:

            sati_scsi_sense_data_construct(
               sequence,
               scsi_io,
               SCSI_STATUS_GOOD,
               SCSI_SENSE_NO_SENSE,
               SCSI_ASC_NO_ADDITIONAL_SENSE,
               SCSI_ASCQ_NO_ADDITIONAL_SENSE
            );

            if( SATI_START_STOP_UNIT_POWER_CONDITION(cdb) == 0 )
            {
               sequence->device->state = SATI_DEVICE_STATE_STOPPED;
            }
            else
            {
               sequence->device->state = SATI_DEVICE_STATE_STANDBY;
            }
            sequence->state = SATI_SEQUENCE_STATE_FINAL;
            break;

         default:
            //FLUSH Cache command does not require any success handling
            break;
      }

      if (sequence->state == SATI_SEQUENCE_STATE_INCOMPLETE)
      {
         return SATI_SEQUENCE_INCOMPLETE;
      }
   }
   return SATI_COMPLETE;
}

#endif // !defined(DISABLE_SATI_START_STOP_UNIT)

