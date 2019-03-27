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
 *        translate the SCSI read (6, 10, 12, or 16-byte) commands.
 */

#include <dev/isci/scil/sati_move.h>
#include <dev/isci/scil/sati_read.h>
#include <dev/isci/scil/sati_callbacks.h>
#include <dev/isci/scil/sati_util.h>

#include <dev/isci/scil/intel_ata.h>
#include <dev/isci/scil/intel_scsi.h>

//******************************************************************************
//* P R I V A T E   M E T H O D S
//******************************************************************************

/**
 * @brief This method performs the common translation functionality for
 *        all SCSI read operations that are 10 bytes in size or larger.
 *        Translated/Written items include:
 *        - Force Unit Access (FUA)
 *        - Sector Count/Transfer Length
 *        - Command register
 *
 * @param[in] sector_count This parameter specifies the number of sectors
 *            to be transferred by this request.
 * @param[in] device_head This parameter points to device head register
 *            that is to be written into the ATA task file (register FIS).
 *
 * @return Indicate if the command translation succeeded.
 * @see sati_move_set_sector_count() for additional return values.
 */
static
SATI_STATUS sati_read_large_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io,
   U32                          sector_count,
   U8                         * device_head
)
{
   sequence->data_direction = SATI_DATA_DIRECTION_IN;

   return sati_move_large_translate_command(
             sequence,
             scsi_io,
             ata_io,
             sector_count,
             device_head
          );
}

/**
 * @brief This method performs the common translation functionality for
 *        all SCSI read operations containing a 32-bit logical block
 *        address.
 *        Translated/Written items include:
 *        - Logical Block Address (LBA)
 *        - Force Unit Access (FUA)
 *        - Sector Count/Transfer Length
 *        - Command register
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @param[in] sector_count This parameter specifies the number of sectors
 *            to be transferred by this request.
 * @param[in] control_byte_offset This parameter specifies the byte offset
 *            into the command descriptor block at which the control byte
 *            is located.
 *
 * @return Indicate if the command translation succeeded.
 * @see sati_move_32_bit_lba_translate_command(), sati_move_set_sector_count()
 *      for additional return values.
 */
static
SATI_STATUS sati_read_32_bit_lba_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io,
   U32                          sector_count,
   U8                           control_byte_offset
)
{
   U8           device_head = 0;
   SATI_STATUS  status;

   status = sati_read_large_translate_command(
               sequence, scsi_io, ata_io, sector_count, &device_head
            );

   if (status == SATI_SUCCESS)
   {
      status = sati_move_32_bit_lba_translate_command(
                  sequence, scsi_io, ata_io, device_head
               );
   }

   return status;
}

//******************************************************************************
//* P U B L I C   M E T H O D S
//******************************************************************************

/**
 * @brief This method will translate the SCSI read command into a
 *        corresponding ATA read 6 command.  Depending upon the capabilities
 *        supported by the target different ATA commands can be selected.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @return Indicate if the command translation succeeded.
 * @retval SCI_SUCCESS This is returned if the command translation was
 *         successful.
 * @retval SATI_FAILURE_CHECK_RESPONSE_DATA This value is returned if
 *         sense data has been created as a result of something specified
 *         in the CDB.
 */
SATI_STATUS sati_read_6_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   if(sati_device_state_stopped(sequence, scsi_io))
   {
      return SATI_FAILURE_CHECK_RESPONSE_DATA;
   }
   else
   {
      sequence->data_direction = SATI_DATA_DIRECTION_IN;
      sequence->type           = SATI_SEQUENCE_READ_6;

      return sati_move_small_translate_command(sequence, scsi_io, ata_io);
   }
}

/**
 * @brief This method will translate the SCSI read 10 command into a
 *        corresponding ATA read command.  Depending upon the capabilities
 *        supported by the target different ATA commands can be selected.
 *        It ensures that all translation required for this command is
 *        performed successfully.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @return Indicate if the command translation succeeded.
 * @see sati_read_32_bit_lba_translate_command() for return values.
 */
SATI_STATUS sati_read_10_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{

   U8 * cdb = sati_cb_get_cdb_address(scsi_io);

   U32 sector_count = (sati_get_cdb_byte(cdb, 7) << 8) |
                      (sati_get_cdb_byte(cdb, 8));

   if(sati_device_state_stopped(sequence, scsi_io))
   {
      return SATI_FAILURE_CHECK_RESPONSE_DATA;
   }
   else
   {
      sequence->type = SATI_SEQUENCE_READ_10;

      return sati_read_32_bit_lba_translate_command(
                sequence, scsi_io, ata_io, sector_count, 9
             );
   }
}

/**
 * @brief This method will translate the SCSI read 12 command into a
 *        corresponding ATA read command.  Depending upon the capabilities
 *        supported by the target different ATA commands can be selected.
 *        It ensures that all translation required for this command is
 *        performed successfully.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @return Indicate if the command translation succeeded.
 * @see sati_read_32_bit_lba_translate_command() for return values.
 */
SATI_STATUS sati_read_12_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   U8  * cdb          = sati_cb_get_cdb_address(scsi_io);
   U32   sector_count = (sati_get_cdb_byte(cdb, 6) << 24) |
                        (sati_get_cdb_byte(cdb, 7) << 16) |
                        (sati_get_cdb_byte(cdb, 8) << 8)  |
                        (sati_get_cdb_byte(cdb, 9));

   if(sati_device_state_stopped(sequence, scsi_io))
   {
      return SATI_FAILURE_CHECK_RESPONSE_DATA;
   }
   else
   {
      sequence->type = SATI_SEQUENCE_READ_12;

      return sati_read_32_bit_lba_translate_command(
                sequence, scsi_io, ata_io, sector_count, 11
             );
   }
}

/**
 * @brief This method will translate the SCSI read 16 command into a
 *        corresponding ATA read command.  Depending upon the capabilities
 *        supported by the target different ATA commands can be selected.
 *        It ensures that all translation required for this command is
 *        performed successfully.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @return Indicate if the command translation succeeded.
 * @see sati_read_large_translate_command(), sati_move_translate_64_bit_lba()
 *      for additional return values.
 */
SATI_STATUS sati_read_16_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   SATI_STATUS   status;
   U8            device_head  = 0;
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
      sequence->type = SATI_SEQUENCE_READ_16;

      // Translate the sector count, write command register, and check various
      // other parts of the CDB.
      status = sati_read_large_translate_command(
                  sequence, scsi_io, ata_io, sector_count, &device_head
               );

      // Attempt to translate the 64-bit LBA field from the SCSI request
      // into the 48-bits of LBA in the ATA register FIS.
      if (status == SATI_SUCCESS)
      {
         sati_move_translate_command(sequence, scsi_io, ata_io, device_head);
         status = sati_move_translate_64_bit_lba(sequence, scsi_io, ata_io);
      }

      return status;
   }
}

