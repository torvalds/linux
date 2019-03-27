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
 * @brief This file contains the method implementations for translating
 *        the SCSI VERIFY (10, 12, 16-byte) commands.
 */

#if !defined(DISABLE_SATI_VERIFY)

#include <dev/isci/scil/sati_verify.h>
#include <dev/isci/scil/sati_callbacks.h>
#include <dev/isci/scil/sati_util.h>
#include <dev/isci/scil/sati_move.h>

#include <dev/isci/scil/intel_ata.h>
#include <dev/isci/scil/intel_scsi.h>
#include <dev/isci/scil/intel_sat.h>

//******************************************************************************
//* P R I V A T E   M E T H O D S
//******************************************************************************

/**
 * @brief This method performs the SCSI VERIFY command translation
 *        functionality common to all VERIFY command sizes.
 *        This includes:
 *        - setting the command register
 *        - setting the device head register
 *        - filling in fields in the SATI_TRANSLATOR_SEQUENCE object.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @return Indicate if the method was successfully completed.
 * @retval SATI_SUCCESS This is returned in all other cases.
 */
static
SATI_STATUS sati_verify_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   U8 * cdb          = sati_cb_get_cdb_address(scsi_io);
   U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);

   /**
    * The translator doesn't support performing the byte check operation.
    * As a result, error the request if the BYTCHK bit is set.
    */
   if ((sati_get_cdb_byte(cdb, 1) & SCSI_VERIFY_BYTCHK_ENABLED))
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

   sequence->protocol       = SAT_PROTOCOL_NON_DATA;
   sequence->data_direction = SATI_DATA_DIRECTION_NONE;

   sati_set_ata_device_head(register_fis, ATA_DEV_HEAD_REG_LBA_MODE_ENABLE);

   // Ensure the device supports the 48 bit feature set.
   if (sequence->device->capabilities & SATI_DEVICE_CAP_48BIT_ENABLE)
      sati_set_ata_command(register_fis, ATA_READ_VERIFY_SECTORS_EXT);
   else
      sati_set_ata_command(register_fis, ATA_READ_VERIFY_SECTORS);

   return SATI_SUCCESS;
}

//******************************************************************************
//* P U B L I C   M E T H O D S
//******************************************************************************

/**
 * @brief This method performs all of the translation required for a
 *        SCSI VERIFY 10 byte CDB.
 *        This includes:
 *        - logical block address translation
 *        - transfer length (sector count) translation
 *        - translation items common to all VERIFY CDB sizes.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @return Indicate if the command translation was successful.
 *         For more information on return values please reference
 *         sati_move_set_sector_count(), sati_verify_translate_command()
 */
SATI_STATUS sati_verify_10_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   SATI_STATUS   status;
   U8          * cdb          = sati_cb_get_cdb_address(scsi_io);
   U32           sector_count = (sati_get_cdb_byte(cdb, 7) << 8) |
                                (sati_get_cdb_byte(cdb, 8));

   if(sati_device_state_stopped(sequence, scsi_io))
   {
      return SATI_FAILURE_CHECK_RESPONSE_DATA;
   }
   else
   {
      sequence->type = SATI_SEQUENCE_VERIFY_10;

      // Fill in the Logical Block Address fields and sector count registers.
      sati_move_translate_32_bit_lba(sequence, scsi_io, ata_io);
      status = sati_move_set_sector_count(sequence,scsi_io,ata_io,sector_count,0);
      if (status != SATI_SUCCESS)
         return status;

      return sati_verify_translate_command(sequence, scsi_io, ata_io);
   }
}

/**
 * @brief This method performs all of the translation required for a
 *        SCSI VERIFY 12 byte CDB.
 *        This includes:
 *        - logical block address translation
 *        - transfer length (sector count) translation
 *        - translation items common to all VERIFY CDB sizes.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @return Indicate if the command translation was successful.
 *         For more information on return values please reference
 *         sati_move_set_sector_count(), sati_verify_translate_command()
 */
SATI_STATUS sati_verify_12_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   SATI_STATUS   status;
   U8          * cdb          = sati_cb_get_cdb_address(scsi_io);
   U32           sector_count = (sati_get_cdb_byte(cdb, 6) << 24) |
                                (sati_get_cdb_byte(cdb, 7) << 16) |
                                (sati_get_cdb_byte(cdb, 8) << 8)  |
                                (sati_get_cdb_byte(cdb, 9));

   if(sati_device_state_stopped(sequence, scsi_io))
   {
      return SATI_FAILURE_CHECK_RESPONSE_DATA;
   }
   else
   {
      sequence->type = SATI_SEQUENCE_VERIFY_12;

      // Fill in the Logical Block Address fields and sector count registers.
      sati_move_translate_32_bit_lba(sequence, scsi_io, ata_io);
      status = sati_move_set_sector_count(sequence,scsi_io,ata_io,sector_count,0);
      if (status != SATI_SUCCESS)
         return status;

      return sati_verify_translate_command(sequence, scsi_io, ata_io);
   }
}

/**
 * @brief This method performs all of the translation required for a
 *        SCSI VERIFY 16 byte CDB.
 *        This includes:
 *        - logical block address translation
 *        - transfer length (sector count) translation
 *        - translation items common to all VERIFY CDB sizes.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @return Indicate if the command translation was successful.
 *         For more information on return values please reference
 *         sati_move_set_sector_count(), sati_verify_translate_command()
 */
SATI_STATUS sati_verify_16_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   SATI_STATUS   status;
   U8          * cdb          = sati_cb_get_cdb_address(scsi_io);
   U32           sector_count = (sati_get_cdb_byte(cdb, 10) << 24) |
                                (sati_get_cdb_byte(cdb, 11) << 16) |
                                (sati_get_cdb_byte(cdb, 12) << 8)  |
                                (sati_get_cdb_byte(cdb, 13));

   if(sati_device_state_stopped(sequence, scsi_io))
   {
      return SATI_FAILURE_CHECK_RESPONSE_DATA;
   }
   else
   {
      sequence->type = SATI_SEQUENCE_VERIFY_16;

      // Fill in the Logical Block Address field.
      status = sati_move_translate_64_bit_lba(sequence, scsi_io, ata_io);
      if (status != SATI_SUCCESS)
         return status;

      // Fill in the Sector Count fields.
      status = sati_move_set_sector_count(sequence,scsi_io,ata_io,sector_count,0);
      if (status != SATI_SUCCESS)
         return status;

      return sati_verify_translate_command(sequence, scsi_io, ata_io);
   }
}

#endif // !defined(DISABLE_SATI_VERIFY)

