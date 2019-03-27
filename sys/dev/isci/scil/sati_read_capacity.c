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
 *        translate the SCSI read capacity (10 byte) command.
 */

#if !defined(DISABLE_SATI_READ_CAPACITY)

#include <dev/isci/scil/sati_read_capacity.h>
#include <dev/isci/scil/sati_callbacks.h>
#include <dev/isci/scil/sati_util.h>
#include <dev/isci/scil/intel_ata.h>
#include <dev/isci/scil/intel_scsi.h>

/**
 * @brief This method will translate the read capacity 10 SCSI command into
 *        an ATA IDENTIFY DEVICE command.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @return Indicate if the command translation succeeded.
 * @retval SCI_SUCCESS This is returned if the command translation was
 *         successful.
 * @retval SATI_FAILURE_CHECK_RESPONSE_DATA This value is returned if the
 *         LBA field is not 0, the PMI bit is not 0.
 */
SATI_STATUS sati_read_capacity_10_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   U8 * cdb = sati_cb_get_cdb_address(scsi_io);

   /**
    * SAT dictates:
    * - the LBA field must be 0
    * - the PMI bit must be 0
    */
   if (
         (
            (sati_get_cdb_byte(cdb, 2) != 0)
         || (sati_get_cdb_byte(cdb, 3) != 0)
         || (sati_get_cdb_byte(cdb, 4) != 0)
         || (sati_get_cdb_byte(cdb, 5) != 0)
         )
         || ((sati_get_cdb_byte(cdb, 8) & SCSI_READ_CAPACITY_PMI_BIT_ENABLE)
              == 1)
      )
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

   // The CDB is properly formed.
   sequence->allocation_length = SCSI_READ_CAPACITY_10_DATA_LENGTH;
   sequence->type              = SATI_SEQUENCE_READ_CAPACITY_10;

   sati_ata_identify_device_construct(ata_io, sequence);
   return SATI_SUCCESS;
}



/**
 * @brief This method will translate the read capacity 16 SCSI command into
 *        an ATA IDENTIFY DEVICE command.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @return Indicate if the command translation succeeded.
 * @retval SCI_SUCCESS This is returned if the command translation was
 *         successful.
 * @retval SATI_FAILURE_CHECK_RESPONSE_DATA This value is returned if the
 *         LBA field is not 0, the PMI bit is not 0.
 */
SATI_STATUS sati_read_capacity_16_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   U8 * cdb = sati_cb_get_cdb_address(scsi_io);

   /**
    * SAT dictates:
    * - the LBA field must be 0
    * - the PMI bit must be 0
    */
   if (
         (
            (sati_get_cdb_byte(cdb, 2) != 0)
         || (sati_get_cdb_byte(cdb, 3) != 0)
         || (sati_get_cdb_byte(cdb, 4) != 0)
         || (sati_get_cdb_byte(cdb, 5) != 0)
         || (sati_get_cdb_byte(cdb, 6) != 0)
         || (sati_get_cdb_byte(cdb, 7) != 0)
         || (sati_get_cdb_byte(cdb, 8) != 0)
         || (sati_get_cdb_byte(cdb, 9) != 0)
         )
         || ((sati_get_cdb_byte(cdb, 14) & SCSI_READ_CAPACITY_PMI_BIT_ENABLE)
              == 1)
      )
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

   // The CDB is properly formed.
   sequence->allocation_length = (sati_get_cdb_byte(cdb, 10) << 24) |
                                 (sati_get_cdb_byte(cdb, 11) << 16) |
                                 (sati_get_cdb_byte(cdb, 12) << 8)  |
                                 (sati_get_cdb_byte(cdb, 13));

   sequence->type              = SATI_SEQUENCE_READ_CAPACITY_16;

   sati_ata_identify_device_construct(ata_io, sequence);
   return SATI_SUCCESS;
}

/**
 * @brief This method will translate the ATA Identify Device data into
 *        SCSI read capacity 10 data.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_data().
 *
 * @return none
 */
void sati_read_capacity_10_translate_data(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * ata_input_data,
   void                       * scsi_io
)
{
   U32  lba_low     = 0;
   U32  lba_high    = 0;
   U32  sector_size = 0;

   // Extract the sector information (sector size, logical blocks) from
   // the retrieved ATA identify device data.
   sati_ata_identify_device_get_sector_info(
      (ATA_IDENTIFY_DEVICE_DATA_T*)ata_input_data,
      &lba_high,
      &lba_low,
      &sector_size
   );

   // SATA drives report a value that is one LBA larger than the last LBA.
   // SCSI wants the last LBA.  Make the correction here.  lba_low is
   // always decremented since it is an unsigned long the value 0 will
   // wrap to 0xFFFFFFFF.
   if ((lba_low == 0) && (lba_high == 0))
      lba_high -= 1;
   lba_low -= 1;

   if(lba_high != 0)
   {
      sati_set_data_byte(sequence, scsi_io, 0, 0xFF);
      sati_set_data_byte(sequence, scsi_io, 1, 0xFF);
      sati_set_data_byte(sequence, scsi_io, 2, 0xFF);
      sati_set_data_byte(sequence, scsi_io, 3, 0xFF);
   }
   else
   {
      // Build CDB for Read Capacity 10
      // Fill in the Logical Block Address bytes.
      sati_set_data_byte(sequence, scsi_io, 0, (U8)((lba_low >> 24) & 0xFF));
      sati_set_data_byte(sequence, scsi_io, 1, (U8)((lba_low >> 16) & 0xFF));
      sati_set_data_byte(sequence, scsi_io, 2, (U8)((lba_low >> 8)  & 0xFF));
      sati_set_data_byte(sequence, scsi_io, 3, (U8)(lba_low & 0xFF));
   }
   // Fill in the sector size field.
   sati_set_data_byte(sequence, scsi_io, 4, (U8)((sector_size >> 24) & 0xFF));
   sati_set_data_byte(sequence, scsi_io, 5, (U8)((sector_size >> 16) & 0xFF));
   sati_set_data_byte(sequence, scsi_io, 6, (U8)((sector_size >> 8)  & 0xFF));
   sati_set_data_byte(sequence, scsi_io, 7, (U8)(sector_size & 0xFF));
}

/**
 * @brief This method will translate the ATA Identify Device data into
 *        SCSI read capacity 16 data.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_data().
 *
 * @return none
 */
void sati_read_capacity_16_translate_data(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * ata_input_data,
   void                       * scsi_io
)
{
   U32  lba_low     = 0;
   U32  lba_high    = 0;
   U32  sector_size = 0;
   ATA_IDENTIFY_DEVICE_DATA_T * identify_device_data;
   U16  physical_per_logical_enable_bit = 0;
   U8   physical_per_logical_sector_exponent = 0;
   U16  physical_per_logical_sector = 0;
   U16  logical_sector_alignment = 0;
   U16  scsi_logical_sector_alignment = 0;
   U8   byte14 = 0;

   //A number of data fields need to be extracted from ATA identify device data
   identify_device_data = (ATA_IDENTIFY_DEVICE_DATA_T*)ata_input_data;

   // Extract the sector information (sector size, logical blocks) from
   // the retrieved ATA identify device data.
   sati_ata_identify_device_get_sector_info(
      (ATA_IDENTIFY_DEVICE_DATA_T*)ata_input_data,
      &lba_high,
      &lba_low,
      &sector_size
   );

   // SATA drives report a value that is one LBA larger than the last LBA.
   // SCSI wants the last LBA.  Make the correction here.  lba_low is
   // always decremented since it is an unsigned long the value 0 will
   // wrap to 0xFFFFFFFF.
   if ((lba_low == 0) && (lba_high == 0))
      lba_high -= 1;
   lba_low -= 1;

   // Build the CDB for Read Capacity 16
   // Fill in the Logical Block Address bytes.
   sati_set_data_byte(sequence, scsi_io, 0, (U8)((lba_high >> 24) & 0xFF));
   sati_set_data_byte(sequence, scsi_io, 1, (U8)((lba_high >> 16) & 0xFF));
   sati_set_data_byte(sequence, scsi_io, 2, (U8)((lba_high >> 8)  & 0xFF));
   sati_set_data_byte(sequence, scsi_io, 3, (U8)(lba_high & 0xFF));

   sati_set_data_byte(sequence, scsi_io, 4, (U8)((lba_low >> 24) & 0xFF));
   sati_set_data_byte(sequence, scsi_io, 5, (U8)((lba_low >> 16) & 0xFF));
   sati_set_data_byte(sequence, scsi_io, 6, (U8)((lba_low >> 8)  & 0xFF));
   sati_set_data_byte(sequence, scsi_io, 7, (U8)(lba_low & 0xFF));

   //Fill in the sector size field.
   sati_set_data_byte(sequence, scsi_io, 8,  (U8)((sector_size >> 24) & 0xFF));
   sati_set_data_byte(sequence, scsi_io, 9,  (U8)((sector_size >> 16) & 0xFF));
   sati_set_data_byte(sequence, scsi_io, 10, (U8)((sector_size >> 8)  & 0xFF));
   sati_set_data_byte(sequence, scsi_io, 11, (U8)(sector_size & 0xFF));

   //Explicitly set byte 12 to 0.  SATI requires that all bytes in the data
   //response be explicitly set to some value.
   sati_set_data_byte(sequence, scsi_io, 12, 0);

   //Check Bit 13 of ATA_IDENTIFY_DEVICE_DATA physical_logical_sector_info
   //(Word 106) is enabled
   physical_per_logical_enable_bit = (identify_device_data->physical_logical_sector_info
      & ATA_IDENTIFY_LOGICAL_SECTOR_PER_PHYSICAL_SECTOR_ENABLE);

   //Extract the Physical per logical sector exponent field and calculate
   //Physical per logical sector value
   physical_per_logical_sector_exponent = (U8) (identify_device_data->physical_logical_sector_info
      & ATA_IDENTIFY_LOGICAL_SECTOR_PER_PHYSICAL_SECTOR_MASK);
   physical_per_logical_sector = 1 << (physical_per_logical_sector_exponent);

   //If the data is valid, fill in the logical blocks per physical block exponent field.
   //Else set logical blocks per physical block exponent to 1
   if (physical_per_logical_enable_bit != 0)
      sati_set_data_byte(
         sequence,
         scsi_io,
         13,
         (U8)(physical_per_logical_sector_exponent & 0xFF)
      );
   else
      sati_set_data_byte(sequence, scsi_io, 13, 0);

   //Fill in the lowest aligned logical block address field.
   logical_sector_alignment = identify_device_data->logical_sector_alignment;
   if (logical_sector_alignment == 0)
      scsi_logical_sector_alignment = 0;
   else
      scsi_logical_sector_alignment = (physical_per_logical_sector - logical_sector_alignment)
         % physical_per_logical_sector;

   //Follow SAT for reporting tprz and tpe
   if ((sequence->device->capabilities & SATI_DEVICE_CAP_DSM_TRIM_SUPPORT) &&
       (sequence->device->capabilities & SATI_DEVICE_CAP_DETERMINISTIC_READ_AFTER_TRIM))
   {
      // tpe
      byte14 |= 0x80;
      // tprz
      if (sequence->device->capabilities & SATI_DEVICE_CAP_READ_ZERO_AFTER_TRIM)
          byte14 |= 0x40;
   }
   sati_set_data_byte(
       sequence,
       scsi_io,
       14,
       (U8)(((scsi_logical_sector_alignment >>8) & 0x3F) | byte14));

   sati_set_data_byte(
       sequence,
       scsi_io,
       15,
       (U8)(scsi_logical_sector_alignment & 0xFF));
}

#endif // !defined(DISABLE_SATI_READ_CAPACITY)

