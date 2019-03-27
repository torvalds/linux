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
#include <dev/isci/scil/sati_callbacks.h>
#include <dev/isci/scil/sati_util.h>
#include <dev/isci/scil/sati_report_luns.h>
#include <dev/isci/scil/sati_inquiry.h>
#include <dev/isci/scil/sati_mode_sense_6.h>
#include <dev/isci/scil/sati_mode_sense_10.h>
#include <dev/isci/scil/sati_mode_select.h>
#include <dev/isci/scil/sati_test_unit_ready.h>
#include <dev/isci/scil/sati_read_capacity.h>
#include <dev/isci/scil/sati_read.h>
#include <dev/isci/scil/sati_write.h>
#include <dev/isci/scil/sati_verify.h>
#include <dev/isci/scil/sati_synchronize_cache.h>
#include <dev/isci/scil/sati_lun_reset.h>
#include <dev/isci/scil/sati_start_stop_unit.h>
#include <dev/isci/scil/sati_request_sense.h>
#include <dev/isci/scil/sati_write_long.h>
#include <dev/isci/scil/sati_reassign_blocks.h>
#include <dev/isci/scil/sati_log_sense.h>
#include <dev/isci/scil/sati_abort_task_set.h>
#include <dev/isci/scil/sati_unmap.h>
#include <dev/isci/scil/sati_passthrough.h>
#include <dev/isci/scil/sati_write_and_verify.h>
#include <dev/isci/scil/sati_read_buffer.h>
#include <dev/isci/scil/sati_write_buffer.h>
#include <dev/isci/scil/intel_ata.h>
#include <dev/isci/scil/intel_scsi.h>
#include <dev/isci/scil/intel_sat.h>

//******************************************************************************
//* P R I V A T E   M E T H O D S
//******************************************************************************

/**
 * @brief This method performs the translation of ATA error register values
 *        into SCSI sense data.
 *        For more information on the parameter passed to this method please
 *        reference the sati_translate_response() method.
 *
 * @param[in] error This parameter specifies the contents of the ATA error
 *            register to be translated.
 *
 * @return none
 */
void sati_translate_error(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   U8                           error
)
{
   if (error & ATA_ERROR_REG_NO_MEDIA_BIT)
   {
      sati_scsi_sense_data_construct(
         sequence,
         scsi_io,
         SCSI_STATUS_CHECK_CONDITION,
         SCSI_SENSE_NOT_READY,
         SCSI_ASC_MEDIUM_NOT_PRESENT,
         SCSI_ASCQ_MEDIUM_NOT_PRESENT
      );
   }
   else if (error & ATA_ERROR_REG_MEDIA_CHANGE_BIT)
   {
      sati_scsi_sense_data_construct(
         sequence,
         scsi_io,
         SCSI_STATUS_CHECK_CONDITION,
         SCSI_SENSE_UNIT_ATTENTION,
         SCSI_ASC_NOT_READY_TO_READY_CHANGE,
         SCSI_ASCQ_NOT_READY_TO_READY_CHANGE
      );
   }
   else if (error & ATA_ERROR_REG_MEDIA_CHANGE_REQUEST_BIT)
   {
      sati_scsi_sense_data_construct(
         sequence,
         scsi_io,
         SCSI_STATUS_CHECK_CONDITION,
         SCSI_SENSE_UNIT_ATTENTION,
         SCSI_ASC_MEDIUM_REMOVAL_REQUEST,
         SCSI_ASCQ_MEDIUM_REMOVAL_REQUEST
      );
   }
   else if (error & ATA_ERROR_REG_ID_NOT_FOUND_BIT)
   {
      sati_scsi_sense_data_construct(
         sequence,
         scsi_io,
         SCSI_STATUS_CHECK_CONDITION,
         SCSI_SENSE_ILLEGAL_REQUEST,
         SCSI_ASC_LBA_OUT_OF_RANGE,
         SCSI_ASCQ_LBA_OUT_OF_RANGE
      );
   }
   else if (error & ATA_ERROR_REG_UNCORRECTABLE_BIT)
   {
      //Mark the Sequence state as a read error so more sense data
      //can be returned later
      sequence->state = SATI_SEQUENCE_STATE_READ_ERROR;
      sati_scsi_sense_data_construct(
         sequence,
         scsi_io,
         SCSI_STATUS_CHECK_CONDITION,
         SCSI_SENSE_MEDIUM_ERROR,
         SCSI_ASC_UNRECOVERED_READ_ERROR,
         SCSI_ASCQ_UNRECOVERED_READ_ERROR
      );
   }
   else if (  (sequence->data_direction == SATI_DATA_DIRECTION_OUT)
           && (error & ATA_ERROR_REG_WRITE_PROTECTED_BIT) )
   {
      sati_scsi_sense_data_construct(
         sequence,
         scsi_io,
         SCSI_STATUS_CHECK_CONDITION,
         SCSI_SENSE_DATA_PROTECT,
         SCSI_ASC_WRITE_PROTECTED,
         SCSI_ASCQ_WRITE_PROTECTED
      );
   }
   else if (error & ATA_ERROR_REG_ICRC_BIT)
   {
      sati_scsi_sense_data_construct(
         sequence,
         scsi_io,
         SCSI_STATUS_CHECK_CONDITION,
         SCSI_SENSE_ABORTED_COMMAND,
         SCSI_ASC_IU_CRC_ERROR_DETECTED,
         SCSI_ASCQ_IU_CRC_ERROR_DETECTED
      );
   }
   else // (error & ATA_ERROR_REG_ABORT_BIT)
   {
      // The ABORT bit has the lowest precedence of all errors.
      // As a result, it is at the bottom of the conditional
      // statement.
      sati_scsi_sense_data_construct(
         sequence,
         scsi_io,
         SCSI_STATUS_CHECK_CONDITION,
         SCSI_SENSE_ABORTED_COMMAND,
         SCSI_ASC_NO_ADDITIONAL_SENSE,
         SCSI_ASCQ_NO_ADDITIONAL_SENSE
      );
   }
}

/**
 * @brief This method translates the supplied ATA payload data into the
 *        corresponding SCSI data.  This is necessary for SCSI commands
 *        that have well-defined payload data associated with them (e.g.
 *        READ CAPACITY).
 *
 * @param[in]  sequence This parameter specifies the sequence
 *             data associated with the translation.
 * @param[in]  ata_io This parameter specifies the ATA payload
 *             buffer location and size to be translated.
 * @param[out] scsi_output_data This parameter specifies the SCSI payload
 *             memory area into which the translator is to write.
 *
 * @return none
 */
static
void sati_translate_data(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * ata_input_data,
   void                       * scsi_io
)
{
   // Update the device capabilities in the odd/crazy event something changed.
   sati_device_update_capabilities(
      sequence->device, (ATA_IDENTIFY_DEVICE_DATA_T*) ata_input_data
   );

   // Look at the first byte to determine the SCSI command to translate.
   switch (sequence->type)
   {
#if !defined(DISABLE_SATI_INQUIRY)
      case SATI_SEQUENCE_INQUIRY_STANDARD:
         sati_inquiry_standard_translate_data(
            sequence, ata_input_data, scsi_io
         );
      break;

      case SATI_SEQUENCE_INQUIRY_SERIAL_NUMBER:
         sati_inquiry_serial_number_translate_data(
            sequence, ata_input_data, scsi_io
         );
      break;

      case SATI_SEQUENCE_INQUIRY_DEVICE_ID:
         sati_inquiry_device_id_translate_data(
            sequence, ata_input_data, scsi_io
         );
      break;

      case SATI_SEQUENCE_INQUIRY_BLOCK_DEVICE:
         sati_inquiry_block_device_translate_data(
            sequence, ata_input_data, scsi_io
         );
      break;

      case SATI_SEQUENCE_INQUIRY_ATA_INFORMATION:
         sati_inquiry_ata_information_translate_data(
            sequence, ata_input_data, scsi_io
         );
      break;

#endif // !defined(DISABLE_SATI_INQUIRY)

#if !defined(DISABLE_SATI_READ_CAPACITY)
      case SATI_SEQUENCE_READ_CAPACITY_10:
         sati_read_capacity_10_translate_data(sequence, ata_input_data, scsi_io);
      break;

      case SATI_SEQUENCE_READ_CAPACITY_16:
         sati_read_capacity_16_translate_data(sequence, ata_input_data, scsi_io);
      break;
#endif // !defined(DISABLE_SATI_READ_CAPACITY)

#if !defined(DISABLE_SATI_MODE_SENSE)
      case SATI_SEQUENCE_MODE_SENSE_6_CACHING:
         sati_mode_sense_6_caching_translate_data(
            sequence, ata_input_data, scsi_io
         );
      break;

      case SATI_SEQUENCE_MODE_SENSE_6_INFORMATIONAL_EXCP_CONTROL:
         sati_mode_sense_6_informational_excp_control_translate_data(
            sequence, ata_input_data, scsi_io
         );
      break;

      case SATI_SEQUENCE_MODE_SENSE_6_READ_WRITE_ERROR:
         sati_mode_sense_6_read_write_error_translate_data(
            sequence, ata_input_data, scsi_io
         );
      break;

      case SATI_SEQUENCE_MODE_SENSE_6_DISCONNECT_RECONNECT:
         sati_mode_sense_6_disconnect_reconnect_translate_data(
            sequence, ata_input_data, scsi_io
         );
      break;

      case SATI_SEQUENCE_MODE_SENSE_6_CONTROL:
         sati_mode_sense_6_control_translate_data(
            sequence, ata_input_data, scsi_io
         );
      break;

      case SATI_SEQUENCE_MODE_SENSE_6_ALL_PAGES:
         sati_mode_sense_6_all_pages_translate_data(
            sequence, ata_input_data, scsi_io
         );
      break;

      case SATI_SEQUENCE_MODE_SENSE_6_POWER_CONDITION:
         sati_mode_sense_6_power_condition_translate_data(
            sequence, ata_input_data, scsi_io
         );
      break;

      case SATI_SEQUENCE_MODE_SENSE_10_POWER_CONDITION:
         sati_mode_sense_10_power_condition_translate_data(
            sequence, ata_input_data, scsi_io
         );
      break;

      case SATI_SEQUENCE_MODE_SENSE_10_CACHING:
         sati_mode_sense_10_caching_translate_data(
            sequence, ata_input_data, scsi_io
         );
      break;

      case SATI_SEQUENCE_MODE_SENSE_10_INFORMATIONAL_EXCP_CONTROL:
         sati_mode_sense_10_informational_excp_control_translate_data(
            sequence, ata_input_data, scsi_io
         );
      break;

      case SATI_SEQUENCE_MODE_SENSE_10_READ_WRITE_ERROR:
         sati_mode_sense_10_read_write_error_translate_data(
            sequence, ata_input_data, scsi_io
         );
      break;

      case SATI_SEQUENCE_MODE_SENSE_10_DISCONNECT_RECONNECT:
         sati_mode_sense_10_disconnect_reconnect_translate_data(
            sequence, ata_input_data, scsi_io
         );
      break;

      case SATI_SEQUENCE_MODE_SENSE_10_CONTROL:
         sati_mode_sense_10_control_translate_data(
            sequence, ata_input_data, scsi_io
         );
      break;

      case SATI_SEQUENCE_MODE_SENSE_10_ALL_PAGES:
         sati_mode_sense_10_all_pages_translate_data(
            sequence, ata_input_data, scsi_io
         );
      break;
#endif // !defined(DISABLE_SATI_MODE_SENSE)

      default:
      break;
   }
}

//******************************************************************************
//* P U B L I C   M E T H O D S
//******************************************************************************

SATI_STATUS sati_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   SATI_DEVICE_T              * sati_device,
   void                       * scsi_io,
   void                       * ata_io
)
{
   SATI_STATUS   status = SATI_FAILURE;
   U8          * cdb = sati_cb_get_cdb_address(scsi_io);

   //No sense response has been set for the translation sequence yet
   sequence->is_sense_response_set          = FALSE;
   // Default to no translation response required
   sequence->is_translate_response_required = FALSE;
   // Assign sati_device to sequence
   sequence->device  = sati_device;

   /**
    * Fail any I/O request with LUN != 0
    */
   if (sati_cb_get_lun(scsi_io) != 0)
   {
      sati_scsi_sense_data_construct(
         sequence,
         scsi_io,
         SCSI_STATUS_CHECK_CONDITION,
         SCSI_SENSE_ILLEGAL_REQUEST,
         SCSI_ASC_LOGICAL_UNIT_NOT_SUPPORTED,
         0
      );
      return SATI_FAILURE_CHECK_RESPONSE_DATA;
   }

   /**
    * SAT dictates:
    * - the NACA bit in the control byte (last byte) must be 0
    */
   if ( (sati_get_cdb_byte(cdb, sati_cb_get_cdb_length(scsi_io) - 1)
         & SCSI_CONTROL_BYTE_NACA_BIT_ENABLE))
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

   /**
    * Per SAT "Error and sense reporting" section.  All subsequent IOs after
    * a device fault should receive INTERNAL TARGET FAILURE sense data.
    */
   if (sati_device->state == SATI_DEVICE_STATE_DEVICE_FAULT_OCCURRED)
   {
      sati_scsi_sense_data_construct(
         sequence,
         scsi_io,
         SCSI_STATUS_CHECK_CONDITION,
         SCSI_SENSE_HARDWARE_ERROR,
         SCSI_ASC_INTERNAL_TARGET_FAILURE,
         SCSI_ASCQ_INTERNAL_TARGET_FAILURE
      );
      return SATI_FAILURE_CHECK_RESPONSE_DATA;
   }

   if(sequence->state == SATI_SEQUENCE_STATE_INITIAL)
   {
      sequence->command_specific_data.scratch = 0;
      sequence->number_data_bytes_set = 0;
   }


#ifdef SATI_TRANSPORT_SUPPORTS_SATA
   {
      U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);
      sati_set_sata_command_flag(register_fis);
      sati_set_sata_fis_type(register_fis, SATA_FIS_TYPE_REGH2D);
   }
#endif // SATI_TRANSPORT_SUPPORTS_SATA

   // Look at the first byte to determine the SCSI command to translate.
   switch (sati_get_cdb_byte(cdb, 0))
   {
#if !defined(DISABLE_SATI_REPORT_LUNS)
      case SCSI_REPORT_LUNS:
         status = sati_report_luns_translate_command(
                     sequence, scsi_io, ata_io
                  );
      break;
#endif // !defined(DISABLE_SATI_REPORT_LUNS)

#if !defined(DISABLE_SATI_INQUIRY)
      case SCSI_INQUIRY:
         status = sati_inquiry_translate_command(
                     sequence, scsi_io, ata_io
                  );
      break;
#endif // !defined(DISABLE_SATI_INQUIRY)

#if !defined(DISABLE_SATI_MODE_SENSE)
      case SCSI_MODE_SENSE_6:
         status = sati_mode_sense_6_translate_command(
                     sequence, scsi_io, ata_io
                  );
      break;

      case SCSI_MODE_SENSE_10:
         status = sati_mode_sense_10_translate_command(
                     sequence, scsi_io, ata_io
                  );
      break;
#endif // !defined(DISABLE_SATI_MODE_SENSE)

#if !defined(DISABLE_SATI_MODE_SELECT)
      case SCSI_MODE_SELECT_6:
         status = sati_mode_select_6_translate_command(
                     sequence, scsi_io, ata_io
                  );
      break;

      case SCSI_MODE_SELECT_10:
         status = sati_mode_select_10_translate_command(
                     sequence, scsi_io, ata_io
                  );
      break;
#endif // !defined(DISABLE_SATI_MODE_SELECT)

#if !defined(DISABLE_SATI_TEST_UNIT_READY)
      case SCSI_TEST_UNIT_READY:
         status = sati_test_unit_ready_translate_command(
                     sequence, scsi_io, ata_io
                  );
      break;
#endif // !defined(DISABLE_SATI_TEST_UNIT_READY)

#if !defined(DISABLE_SATI_READ_CAPACITY)
      case SCSI_READ_CAPACITY_10:
         status = sati_read_capacity_10_translate_command(
                     sequence, scsi_io, ata_io
                  );
      break;

      case SCSI_SERVICE_ACTION_IN_16:
         if ( (sati_get_cdb_byte(cdb, 1) & SCSI_SERVICE_ACTION_MASK)
              == SCSI_SERVICE_ACTION_IN_CODES_READ_CAPACITY_16)
            status = sati_read_capacity_16_translate_command(
                        sequence, scsi_io, ata_io
                     );
         else
            status = SATI_FAILURE_CHECK_RESPONSE_DATA;
      break;
#endif // !defined(DISABLE_SATI_READ_CAPACITY)

#if !defined(DISABLE_SATI_REQUEST_SENSE)
      case SCSI_REQUEST_SENSE:
         status = sati_request_sense_translate_command(
                     sequence, scsi_io, ata_io
                  );
      break;
#endif // !defined(DISABLE_SATI_REQUEST_SENSE)

      case SCSI_READ_6:
         status = sati_read_6_translate_command(sequence, scsi_io, ata_io);
      break;

      case SCSI_READ_10:
         status = sati_read_10_translate_command(sequence, scsi_io, ata_io);
      break;

      case SCSI_READ_12:
         status = sati_read_12_translate_command(sequence, scsi_io, ata_io);
      break;

      case SCSI_READ_16:
         status = sati_read_16_translate_command(sequence, scsi_io, ata_io);
      break;

      case SCSI_WRITE_6:
         status = sati_write_6_translate_command(sequence, scsi_io, ata_io);
      break;

      case SCSI_WRITE_10:
         status = sati_write_10_translate_command(sequence, scsi_io, ata_io);
      break;

      case SCSI_WRITE_12:
         status = sati_write_12_translate_command(sequence, scsi_io, ata_io);
      break;

      case SCSI_WRITE_16:
         status = sati_write_16_translate_command(sequence, scsi_io, ata_io);
      break;

#if !defined(DISABLE_SATI_VERIFY)
      case SCSI_VERIFY_10:
         status = sati_verify_10_translate_command(sequence, scsi_io, ata_io);
      break;

      case SCSI_VERIFY_12:
         status = sati_verify_12_translate_command(sequence, scsi_io, ata_io);
      break;

      case SCSI_VERIFY_16:
         status = sati_verify_16_translate_command(sequence, scsi_io, ata_io);
      break;
#endif // !defined(DISABLE_SATI_VERIFY)

#if    !defined(DISABLE_SATI_WRITE_AND_VERIFY)  \
   && !defined(DISABLE_SATI_VERIFY)        \
   && !defined(DISABLE_SATI_WRITE)

      case SCSI_WRITE_AND_VERIFY_10:
         status = sati_write_and_verify_10_translate_command(sequence, scsi_io, ata_io);
      break;

      case SCSI_WRITE_AND_VERIFY_12:
         status = sati_write_and_verify_12_translate_command(sequence, scsi_io, ata_io);
      break;

      case SCSI_WRITE_AND_VERIFY_16:
         status = sati_write_and_verify_16_translate_command(sequence, scsi_io, ata_io);
      break;
#endif //    !defined(DISABLE_SATI_WRITE_AND_VERIFY)
      // && !defined(DISABLE_SATI_VERIFY)
      // && !defined(DISABLE_SATI_WRITE)

#if !defined(DISABLE_SATI_REASSIGN_BLOCKS)
      case SCSI_REASSIGN_BLOCKS:
         status = sati_reassign_blocks_translate_command(sequence, scsi_io, ata_io);
      break;
#endif // !defined(DISABLE_SATI_REASSIGN_BLOCKS)

#if !defined(DISABLE_SATI_SYNCHRONIZE_CACHE)
      case SCSI_SYNCHRONIZE_CACHE_10:
      case SCSI_SYNCHRONIZE_CACHE_16:
         status = sati_synchronize_cache_translate_command(sequence, scsi_io, ata_io);
      break;
#endif // !defined(DISABLE_SATI_SYNCHRONIZE_CACHE)

#if !defined(DISABLE_SATI_START_STOP_UNIT)
      case SCSI_START_STOP_UNIT:
         status = sati_start_stop_unit_translate_command(
                     sequence, scsi_io, ata_io
                  );
      break;
#endif // !defined(DISABLE_SATI_START_STOP_UNIT)

#if !defined(DISABLE_SATI_WRITE_LONG)
      case SCSI_WRITE_LONG_10:
      case SCSI_WRITE_LONG_16:
         status = sati_write_long_translate_command(sequence, scsi_io, ata_io);
      break;
#endif // !defined(DISABLE_SATI_WRITE_LONG)

#if !defined(DISABLE_SATI_LOG_SENSE)
      case SCSI_LOG_SENSE:
         status = sati_log_sense_translate_command(sequence, scsi_io, ata_io);
      break;
#endif // !defined(DISABLE_SATI_LOG_SENSE)

      case SCSI_PERSISTENT_RESERVE_IN:
      case SCSI_PERSISTENT_RESERVE_OUT:
         //These commands are not supported by SATI
         sati_scsi_sense_data_construct(
            sequence,
            scsi_io,
            SCSI_STATUS_CHECK_CONDITION,
            SCSI_SENSE_ILLEGAL_REQUEST,
            SCSI_ASC_INVALID_COMMAND_OPERATION_CODE,
            SCSI_ASCQ_INVALID_COMMAND_OPERATION_CODE
         );
         //returning status now to keep sense data set above
         return SATI_FAILURE_CHECK_RESPONSE_DATA;
      break;

#if !defined(DISABLE_SATI_UNMAP)
      case SCSI_UNMAP:
         status = sati_unmap_translate_command(sequence, scsi_io, ata_io);
      break;
#endif // !defined(DISABLE_SATI_UNMAP)

#if !defined(DISABLE_SATI_ATA_PASSTHROUGH)
      case SCSI_ATA_PASSTHRU_12:
          status = sati_passthrough_12_translate_command(sequence, scsi_io, ata_io);
      break;

      case SCSI_ATA_PASSTHRU_16:
          status = sati_passthrough_16_translate_command(sequence, scsi_io, ata_io);
      break;

#endif // !define(DISABLE_SATI_ATA_PASSTHRU)

#if !defined(DISABLE_SATI_READ_BUFFER)
      case SCSI_READ_BUFFER:
         status = sati_read_buffer_translate_command(sequence, scsi_io, ata_io);
      break;
#endif //!defined(DISABLE_SATI_READ_BUFFER)

#if !defined(DISABLE_SATI_WRITE_BUFFER)
      case SCSI_WRITE_BUFFER:
         status = sati_write_buffer_translate_command(sequence, scsi_io, ata_io);
      break;
#endif //!defined(DISABLE_SATI_WRITE_BUFFER)
      default:
         status = SATI_FAILURE_CHECK_RESPONSE_DATA;
      break;
   }

   if( (status == SATI_FAILURE_CHECK_RESPONSE_DATA) &&
       !(sequence->is_sense_response_set) )
   {
      sati_scsi_sense_data_construct(
         sequence,
         scsi_io,
         SCSI_STATUS_CHECK_CONDITION,
         SCSI_SENSE_ILLEGAL_REQUEST,
         SCSI_ASC_INVALID_FIELD_IN_CDB,
         SCSI_ASCQ_INVALID_FIELD_IN_CDB
      );
   }
   return status;
}

// -----------------------------------------------------------------------------

#if !defined(DISABLE_SATI_TASK_MANAGEMENT)
SATI_STATUS sati_translate_task_management(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   SATI_DEVICE_T              * sati_device,
   void                       * scsi_task,
   void                       * ata_io
)
{
   SATI_STATUS status=SATI_FAILURE;
   U8 task_function = sati_cb_get_task_function(scsi_task);

   sequence->device = sati_device;

   switch (task_function)
   {
      /**
       * @todo We need to update the ABORT_TASK and ABORT_TASK_SET to be
       *       SAT compliant.
       */
      case SCSI_TASK_REQUEST_ABORT_TASK:
      case SCSI_TASK_REQUEST_LOGICAL_UNIT_RESET:
         status = sati_lun_reset_translate_command(sequence, scsi_task, ata_io);
      break;

      case SCSI_TASK_REQUEST_ABORT_TASK_SET:
#if !defined(DISABLE_SATI_ABORT_TASK_SET)
         status = sati_abort_task_set_translate_command(sequence, scsi_task, ata_io);
#else
         status = SATI_FAILURE;
#endif
         break;
      default:
         status = SATI_FAILURE;
      break;
   }

   return status;
}
#endif // !defined(DISABLE_SATI_TASK_MANAGEMENT)

// -----------------------------------------------------------------------------
#if      !defined(DISABLE_SATI_INQUIRY)            \
      || !defined(DISABLE_SATI_READY_CAPACITY)     \
      || !defined(DISABLE_SATI_MODE_SENSE)         \
      || !defined(DISABLE_SATI_MODE_SELECT)        \
      || !defined(DISABLE_SATI_REASSIGN_BLOCKS)    \
      || !defined(DISABLE_SATI_START_STOP_UNIT)    \
      || !defined(DISABLE_SATI_REQUEST_SENSE)      \
      || !defined(DISABLE_SATI_WRITE_LONG)         \
      || !defined(DISABLE_SATI_LOG_SENSE)          \
      || !defined(DISABLE_SATI_UNMAP)

static
SATI_STATUS sati_check_data_io(
   SATI_TRANSLATOR_SEQUENCE_T * sequence
)
{
   if(sequence->state == SATI_SEQUENCE_STATE_INCOMPLETE)
   {
      return SATI_SEQUENCE_INCOMPLETE;
   }
   else if(sequence->number_data_bytes_set < sequence->allocation_length)
   {
      return SATI_COMPLETE_IO_DONE_EARLY;
   }
   else
   {
      return SATI_COMPLETE;
   }
}
#endif   //    !defined(DISABLE_SATI_INQUIRY)
         // || !defined(DISABLE_SATI_READY_CAPACITY)
         // || !defined(DISABLE_SATI_MODE_SENSE)
         // || !defined(DISABLE_SATI_MODE_SELECT)
         // || !defined(DISABLE_SATI_REASSIGN_BLOCKS)
         // || !defined(DISABLE_SATI_START_STOP_UNIT)
         // || !defined(DISABLE_SATI_REQUEST_SENSE)
         // || !defined(DISABLE_SATI_WRITE_LONG)
         // || !defined(DISABLE_SATI_LOG_SENSE)
         // || !defined(DISABLE_SATI_UNMAP)
// -----------------------------------------------------------------------------
SATI_STATUS sati_translate_command_response(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   SATI_STATUS   status       = SATI_COMPLETE;
   U8          * register_fis = sati_cb_get_d2h_register_fis_address(ata_io);
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

      // Make sure that the terminate sequence is called to allow
      // translation logic to perform any cleanup before the IO is completed.
      sati_sequence_terminate(sequence,
                              scsi_io,
                              ata_io);

      return SATI_FAILURE_CHECK_RESPONSE_DATA;
   }

   // Look at the sequence type to determine the response translation method
   // to invoke.
   switch (sequence->type)
   {
#if !defined(DISABLE_SATI_TEST_UNIT_READY)
      case SATI_SEQUENCE_TEST_UNIT_READY:
         status = sati_test_unit_ready_translate_response(
                     sequence, scsi_io, ata_io
                  );
      break;
#endif // !defined(DISABLE_SATI_TEST_UNIT_READY)

#if    !defined(DISABLE_SATI_INQUIRY)        \
    || !defined(DISABLE_SATI_READY_CAPACITY) \
    || !defined(DISABLE_SATI_MODE_SENSE)

      case SATI_SEQUENCE_INQUIRY_EXECUTE_DEVICE_DIAG:

         if (ata_status & ATA_STATUS_REG_ERROR_BIT)
         {
            U8  error = (U8) sati_get_ata_error(register_fis);
            status    = SATI_FAILURE_CHECK_RESPONSE_DATA;
            sati_translate_error(sequence, scsi_io, error);
         }
         else
         {
            sati_inquiry_ata_information_finish_translation(
               sequence,
               scsi_io,
               ata_io
            );
            status = sati_check_data_io(sequence);
         }
      break;

      case SATI_SEQUENCE_INQUIRY_STANDARD:
      case SATI_SEQUENCE_INQUIRY_SUPPORTED_PAGES:
      case SATI_SEQUENCE_INQUIRY_SERIAL_NUMBER:
      case SATI_SEQUENCE_INQUIRY_BLOCK_DEVICE:
      case SATI_SEQUENCE_INQUIRY_ATA_INFORMATION:
      case SATI_SEQUENCE_INQUIRY_DEVICE_ID:
      case SATI_SEQUENCE_READ_CAPACITY_10:
      case SATI_SEQUENCE_READ_CAPACITY_16:
      case SATI_SEQUENCE_MODE_SENSE_6_CACHING:
      case SATI_SEQUENCE_MODE_SENSE_6_INFORMATIONAL_EXCP_CONTROL:
      case SATI_SEQUENCE_MODE_SENSE_6_READ_WRITE_ERROR:
      case SATI_SEQUENCE_MODE_SENSE_6_DISCONNECT_RECONNECT:
      case SATI_SEQUENCE_MODE_SENSE_6_CONTROL:
      case SATI_SEQUENCE_MODE_SENSE_6_POWER_CONDITION:
      case SATI_SEQUENCE_MODE_SENSE_6_ALL_PAGES:
      case SATI_SEQUENCE_MODE_SENSE_10_CACHING:
      case SATI_SEQUENCE_MODE_SENSE_10_INFORMATIONAL_EXCP_CONTROL:
      case SATI_SEQUENCE_MODE_SENSE_10_READ_WRITE_ERROR:
      case SATI_SEQUENCE_MODE_SENSE_10_CONTROL:
      case SATI_SEQUENCE_MODE_SENSE_10_POWER_CONDITION:
      case SATI_SEQUENCE_MODE_SENSE_10_DISCONNECT_RECONNECT:
      case SATI_SEQUENCE_MODE_SENSE_10_ALL_PAGES:
         // Did an error occur during the IO request?
         if (ata_status & ATA_STATUS_REG_ERROR_BIT)
         {
            U8  error = (U8) sati_get_ata_error(register_fis);
            status    = SATI_FAILURE_CHECK_RESPONSE_DATA;
            sati_translate_error(sequence, scsi_io, error);
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
               sati_translate_data(sequence, ata_data, scsi_io);
               status = sati_check_data_io(sequence);
            }
         }
      break;
#endif //    !defined(DISABLE_SATI_INQUIRY)
       // && !defined(DISABLE_SATI_READY_CAPACITY)
       // && !defined(DISABLE_SATI_MODE_SENSE)

#if !defined(DISABLE_SATI_MODE_SELECT)
      case SATI_SEQUENCE_MODE_SELECT_MODE_PAGE_CACHING:

         status = sati_mode_select_translate_response(
            sequence, scsi_io, ata_io
               );
         if(status == SATI_COMPLETE)
         {
            status = sati_check_data_io(sequence);
         }
         break;

      case SATI_SEQUENCE_MODE_SELECT_MODE_POWER_CONDITION:
      case SATI_SEQUENCE_MODE_SELECT_MODE_INFORMATION_EXCEPT_CONTROL:
         // Did an error occur during the IO request?
         if (ata_status & ATA_STATUS_REG_ERROR_BIT)
         {
            U8  error = (U8) sati_get_ata_error(register_fis);
            status    = SATI_FAILURE_CHECK_RESPONSE_DATA;
            sati_translate_error(sequence, scsi_io, error);
         }
         else
         {
            status = sati_check_data_io(sequence);
         }
      break;
#endif // !defined(DISABLE_SATI_MODE_SELECT)

#if !defined(DISABLE_SATI_WRITE_AND_VERIFY)
      case SATI_SEQUENCE_WRITE_AND_VERIFY:

         if (ata_status & ATA_STATUS_REG_ERROR_BIT)
         {
            U8  error = (U8) sati_get_ata_error(register_fis);
            sati_translate_error(sequence, scsi_io, error);

            return SATI_FAILURE_CHECK_RESPONSE_DATA;
         }
         else
         {
            status = sati_write_and_verify_translate_response(
                        sequence,
                        scsi_io,
                        ata_io
                     );
         }
      break;
#endif // !defined(DISABLE_SATI_WRITE_AND_VERIFY)

      case SATI_SEQUENCE_READ_6:
      case SATI_SEQUENCE_READ_10:
      case SATI_SEQUENCE_READ_12:
      case SATI_SEQUENCE_READ_16:
      case SATI_SEQUENCE_WRITE_6:
      case SATI_SEQUENCE_WRITE_10:
      case SATI_SEQUENCE_WRITE_12:
      case SATI_SEQUENCE_WRITE_16:
      case SATI_SEQUENCE_VERIFY_10:
      case SATI_SEQUENCE_VERIFY_12:
      case SATI_SEQUENCE_VERIFY_16:
      case SATI_SEQUENCE_SYNCHRONIZE_CACHE:
         if (ata_status & ATA_STATUS_REG_ERROR_BIT)
         {
            U8  error = (U8) sati_get_ata_error(register_fis);
            status    = SATI_FAILURE_CHECK_RESPONSE_DATA;
            sati_translate_error(sequence, scsi_io, error);

            if(sequence->state == SATI_SEQUENCE_STATE_READ_ERROR )
            {
               sati_scsi_read_error_sense_construct(
                  sequence,
                  scsi_io,
                  ata_io,
                  SCSI_STATUS_CHECK_CONDITION,
                  SCSI_SENSE_MEDIUM_ERROR,
                  SCSI_ASC_UNRECOVERED_READ_ERROR,
                  SCSI_ASCQ_UNRECOVERED_READ_ERROR
               );
               sequence->state = SATI_SEQUENCE_STATE_FINAL;
            }
         }
         else
         {
            // We haven't satisified the transfer count from the original
            // SCSI CDB.  As a result, we need to re-issue the command
            // with updated logical block address and transfer count.
            if (sequence->command_specific_data.scratch)
            {
               /** @todo update the contents of the CDB directly?  Should be
                *  done during previous command translation?
                */
               status = SATI_SEQUENCE_INCOMPLETE;
            }
         }
      break;

#if !defined(DISABLE_SATI_READ_BUFFER)
      case SATI_SEQUENCE_READ_BUFFER:
         status = sati_read_buffer_translate_response(
                     sequence, scsi_io, ata_io
                  );

         if(status == SATI_COMPLETE)
         {
            status = sati_check_data_io(sequence);
         }
      break;
#endif //!defined(DISABLE_SATI_READ_BUFFER)

#if !defined(DISABLE_SATI_WRITE_BUFFER)
      case SATI_SEQUENCE_WRITE_BUFFER:
      case SATI_SEQUENCE_WRITE_BUFFER_MICROCODE:
         status = sati_write_buffer_translate_response(
                     sequence, scsi_io, ata_io
                  );
      break;
#endif //!defined(DISABLE_SATI_WRITE_BUFFER)

#if !defined(DISABLE_SATI_REASSIGN_BLOCKS)
      case SATI_SEQUENCE_REASSIGN_BLOCKS:
         status = sati_reassign_blocks_translate_response(
                     sequence, scsi_io, ata_io
                  );
         if(status == SATI_COMPLETE)
         {
            status = sati_check_data_io(sequence);
         }
      break;
#endif // !defined(DISABLE_SATI_REASSIGN_BLOCKS)

#if !defined(DISABLE_SATI_START_STOP_UNIT)
      case SATI_SEQUENCE_START_STOP_UNIT:
         status = sati_start_stop_unit_translate_response(
                     sequence, scsi_io, ata_io
                  );
         if(status == SATI_COMPLETE)
         {
            status = sati_check_data_io(sequence);
         }
      break;
#endif // !defined(DISABLE_SATI_START_STOP_UNIT)

#if !defined(DISABLE_SATI_REQUEST_SENSE)
      case SATI_SEQUENCE_REQUEST_SENSE_SMART_RETURN_STATUS:
      case SATI_SEQUENCE_REQUEST_SENSE_CHECK_POWER_MODE:
         status = sati_request_sense_translate_response(
                     sequence, scsi_io, ata_io
                  );
         if(status == SATI_COMPLETE)
         {
            status = sati_check_data_io(sequence);
         }
      break;
#endif // !defined(DISABLE_SATI_REQUEST_SENSE)

#if !defined(DISABLE_SATI_WRITE_LONG)
      case SATI_SEQUENCE_WRITE_LONG:
         status = sati_write_long_translate_response(
                     sequence, scsi_io, ata_io
                  );
         if(status == SATI_COMPLETE)
         {
            status = sati_check_data_io(sequence);
         }
      break;
#endif // !defined(DISABLE_SATI_WRITE_LONG)

#if !defined(DISABLE_SATI_LOG_SENSE)
      case SATI_SEQUENCE_LOG_SENSE_SUPPORTED_LOG_PAGE:
      case SATI_SEQUENCE_LOG_SENSE_SELF_TEST_LOG_PAGE:
      case SATI_SEQUENCE_LOG_SENSE_EXTENDED_SELF_TEST_LOG_PAGE:
      case SATI_SEQUENCE_LOG_SENSE_INFO_EXCEPTION_LOG_PAGE:
         status = sati_log_sense_translate_response(
                     sequence, scsi_io, ata_io
                  );
         if(status == SATI_COMPLETE)
         {
            status = sati_check_data_io(sequence);
         }
      break;
#endif // !defined(DISABLE_SATI_LOG_SENSE)

#if !defined(DISABLE_SATI_UNMAP)
      case SATI_SEQUENCE_UNMAP:
         status = sati_unmap_translate_response(
                     sequence, scsi_io, ata_io
                  );
      break;
#endif // !defined(DISABLE_SATI_UNMAP)

#if !defined(DISABLE_SATI_ATA_PASSTHROUGH)
      case SATI_SEQUENCE_ATA_PASSTHROUGH_12:
      case SATI_SEQUENCE_ATA_PASSTHROUGH_16:
         status = sati_passthrough_translate_response(
                     sequence, scsi_io, ata_io
                  );
      break;
#endif // !defined(DISABLE_SATI_ATA_PASSTHROUGH)

      default:
         status = SATI_FAILURE_INVALID_SEQUENCE_TYPE;
      break;
   }

   return status;
}

// -----------------------------------------------------------------------------

#if !defined(DISABLE_SATI_TASK_MANAGEMENT)
SATI_STATUS sati_translate_task_response(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   SATI_STATUS   status       = SATI_FAILURE_CHECK_RESPONSE_DATA;
   U8          * register_fis = sati_cb_get_d2h_register_fis_address(ata_io);
   U8            ata_status;

   /**
    * If the device fault bit is set in the status register, then
    * set the sense data and return.
    */
   ata_status = (U8) sati_get_ata_status(register_fis);
   if (ata_status & ATA_STATUS_REG_DEVICE_FAULT_BIT)
   {
      sati_scsi_response_data_construct(
         sequence,
         scsi_io,
         SCSI_TASK_MGMT_FUNC_FAILED
      );
      return SATI_FAILURE_CHECK_RESPONSE_DATA;
   }

   // Look at the sequence type to determine the response translation method
   // to invoke.
   switch (sequence->type)
   {
      case SATI_SEQUENCE_LUN_RESET:
         if (ata_status & ATA_STATUS_REG_ERROR_BIT)
         {
            sati_scsi_response_data_construct(
               sequence, scsi_io, SCSI_TASK_MGMT_FUNC_FAILED);
         }
         else
         {
            sati_scsi_response_data_construct(
               sequence, scsi_io, SCSI_TASK_MGMT_FUNC_COMPLETE);
         }

         status = SATI_COMPLETE;
      break;

#if !defined(DISABLE_SATI_ABORT_TASK_SET)
      case SATI_SEQUENCE_ABORT_TASK_SET:
         if (ata_status & ATA_STATUS_REG_ERROR_BIT)
         {
            sati_scsi_response_data_construct(
               sequence, scsi_io, SCSI_TASK_MGMT_FUNC_FAILED);
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
               status = sati_abort_task_set_translate_data(
                           sequence,
                           ata_data,
                           scsi_io
                        );
            }
         }
      break;
#endif // !defined(DISABLE_SATI_ABORT_TASK_SET)

      default:
         status = SATI_FAILURE_INVALID_SEQUENCE_TYPE;
      break;
   }

   return status;
}
#endif // !defined(DISABLE_SATI_TASK_MANAGEMENT)

#if !defined(ENABLE_MINIMUM_MEMORY_MODE)
U32 sati_get_sat_compliance_version(
   void
)
{
   return 2;  // Compliant with SAT-2.
}

U32 sati_get_sat_compliance_version_revision(
   void
)
{
   return 7;  // Compliant with SAT-2 revision 7.
}

#endif // !defined(ENABLE_MINIMUM_MEMORY_MODE)

U16 sati_get_number_data_bytes_set(
   SATI_TRANSLATOR_SEQUENCE_T * sequence
)
{
   return sequence->number_data_bytes_set;
}

void sati_sequence_construct(
   SATI_TRANSLATOR_SEQUENCE_T * sequence
)
{
   sequence->state = SATI_SEQUENCE_STATE_INITIAL;
}

void sati_sequence_terminate(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   // Decode the sequence type to determine how to handle the termination
   // of the translation method.
   switch (sequence->type)
   {
   case SATI_SEQUENCE_UNMAP:
      sati_unmap_terminate(sequence,scsi_io,ata_io);
   break;
   }
}
