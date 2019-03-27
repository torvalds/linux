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
 * @brief This file contains the method implementations common to
 *        translations that move data (i.e. read, write).  It has code for
 *        the various different size CDBs (6, 10, 12, 16).
 */

#include <dev/isci/scil/sati_move.h>
#include <dev/isci/scil/sati_callbacks.h>
#include <dev/isci/scil/sati_translator_sequence.h>
#include <dev/isci/scil/sati_util.h>
#include <dev/isci/scil/intel_ata.h>
#include <dev/isci/scil/intel_scsi.h>
#include <dev/isci/scil/intel_sat.h>

//******************************************************************************
//* P R I V A T E   M E T H O D S
//******************************************************************************

/**
 * @brief This method simply sets the command register based upon the
 *        supplied opcodes and the data direction.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command()
 *
 * @param[in] write_opcode This parameter specifies the value to be written
 *            to the ATA command register for a write (data out) operation.
 * @param[in] read_opcode This parameter specifies the value to be written
 *            to the ATA command register for a read (data in) operation.
 *
 * @return none.
 */
static
void sati_move_set_ata_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * ata_io,
   U8                           write_opcode,
   U8                           read_opcode
)
{
   U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);

   if (sequence->data_direction == SATI_DATA_DIRECTION_OUT)
      sati_set_ata_command(register_fis, write_opcode);
   else
      sati_set_ata_command(register_fis, read_opcode);
}

/**
 * @brief This method will translate the SCSI transfer count from the 6-byte
 *        CDB into the appropriate amount in the ATA register FIS.  Please
 *        note for 48-bit UDMA requests, the caller must set the sector
 *        count extended field.  This method also sets protocol and
 *        command fields.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command()
 *
 * @param[in] write_opcode This parameter specifies the value to be written
 *            to the ATA command register for a write (data out) operation.
 * @param[in] read_opcode This parameter specifies the value to be written
 *            to the ATA command register for a read (data in) operation.
 *
 * @return none
 */
static
void sati_move_small_udma_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io,
   U8                           write_opcode,
   U8                           read_opcode
)
{
   U8 * cdb          = sati_cb_get_cdb_address(scsi_io);
   U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);

   sati_move_set_ata_command(sequence, ata_io, write_opcode, read_opcode);
   sati_set_ata_sector_count(register_fis, sati_get_cdb_byte(cdb, 4));

   if (sequence->data_direction == SATI_DATA_DIRECTION_IN)
      sequence->protocol = SAT_PROTOCOL_UDMA_DATA_IN;
   else
      sequence->protocol = SAT_PROTOCOL_UDMA_DATA_OUT;
}

/**
 * @brief This method will translate the SCSI transfer count from the
 *        supplied sector_count parameter into the ATA register FIS.
 *        The translation is specific to 10,12, 16 byte CDBs.
 *        This method also sets protocol and command fields.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command()
 *
 * @param[in] sector_count This parameter specifies the number of sectors
 *            to be transferred.
 * @param[in] write_opcode This parameter specifies the value to be written
 *            to the ATA command register for a write (data out) operation.
 * @param[in] read_opcode This parameter specifies the value to be written
 *            to the ATA command register for a read (data in) operation.
 *
 * @return Please reference sati_move_set_sector_count() for information
 *         on return codes from this method.
 */
static
SATI_STATUS sati_move_large_udma_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io,
   U32                          sector_count,
   U8                           write_opcode,
   U8                           read_opcode
)
{
   sati_move_set_ata_command(sequence, ata_io, write_opcode, read_opcode);

   if (sequence->data_direction == SATI_DATA_DIRECTION_IN)
      sequence->protocol = SAT_PROTOCOL_UDMA_DATA_IN;
   else
      sequence->protocol = SAT_PROTOCOL_UDMA_DATA_OUT;

   return sati_move_set_sector_count(
             sequence, scsi_io, ata_io, sector_count, FALSE
          );
}

/**
 * @brief This method will translate the SCSI transfer count from the 6-byte
 *        CDB into the appropriate amount in the ATA register FIS.
 *        This is only used for translation of 6-byte SCSI CDBs.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command()
 *
 * @return none
 */
static
void sati_move_ncq_translate_8_bit_sector_count(
   void * scsi_io,
   void * ata_io
)
{
   U8 * cdb          = sati_cb_get_cdb_address(scsi_io);
   U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);

   sati_set_ata_features(register_fis, sati_get_cdb_byte(cdb, 4));

   // A read 6 with a 0 sector count indicates a transfer of 256 sectors.
   // As a result update the MSB (features expanded register) to indicate
   // 256 sectors (0x100).
   if (sati_get_cdb_byte(cdb, 4) == 0)
      sati_set_ata_features_exp(register_fis, 1);
}

//******************************************************************************
//* P U B L I C   M E T H O D S
//******************************************************************************

/**
 * @brief This method will process a 32-bit sector into the appropriate fields
 *        in a register FIS.  This method works for both 8-bit and 16-bit sector
 *        counts.
 *        This is used for translation of 10, 12, and 16-byte SCSI CDBs.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @note This method should only be called for CDB sizes of 10-bytes or larger.
 *
 * @param[in] sector_count This parameter specifies the number of sectors
 *            to be transferred.
 * @param[in] is_fpdma_command This parameter indicates if the supplied
 *            ata_io is a first party DMA request (NCQ).
 *
 * @return none
 */
SATI_STATUS sati_move_set_sector_count(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io,
   U32                          sector_count,
   U8                           is_fpdma_command
)
{
   U32  max_sector_count;
   U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);

   if (sequence->device->capabilities & SATI_DEVICE_CAP_48BIT_ENABLE)
      max_sector_count = 65536;
   else
      max_sector_count = 256;

   // Check the CDB transfer length count and set the register FIS sector
   // count fields
   if (0 == sector_count)
   {
      // A SCSI sector count of 0 for 10-byte CDBs and larger indicate no data
      // transfer, so simply complete the command immediately.
      return SATI_COMPLETE;
   }
   else if (sector_count >= max_sector_count)
   {
      // We have to perform multiple SATA commands to satisfy the sector
      // count specified in the SCSI command.
      sequence->command_specific_data.move_sector_count =
         sector_count - max_sector_count;

      // In ATA a sector count of 0 indicates use the maximum allowed for
      // the command (i.e. 0 == 2^16 or 2^8).
      sector_count = 0;
   }

   if (is_fpdma_command)
   {
      sati_set_ata_features(register_fis, sector_count & 0xFF);
      sati_set_ata_features_exp(register_fis, (sector_count >> 8) & 0xFF);
   }
   else
   {
      sati_set_ata_sector_count(register_fis, sector_count & 0xFF);
      sati_set_ata_sector_count_exp(register_fis, (sector_count >> 8) & 0xFF);
   }

   return SATI_SUCCESS;
}

/**
 * @brief This method simply translates the 32-bit logical block address
 *        field from the SCSI CDB (10 or 12-byte) into the ATA task
 *        file (register FIS).
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command()
 *
 * @return none
 */
void sati_move_translate_32_bit_lba(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   U8 * cdb          = sati_cb_get_cdb_address(scsi_io);
   U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);

   sati_set_ata_lba_low(register_fis, sati_get_cdb_byte(cdb, 5));
   sati_set_ata_lba_mid(register_fis, sati_get_cdb_byte(cdb, 4));
   sati_set_ata_lba_high(register_fis, sati_get_cdb_byte(cdb, 3));
   sati_set_ata_lba_low_exp(register_fis, sati_get_cdb_byte(cdb, 2));
   sati_set_ata_lba_mid_exp(register_fis, 0);
   sati_set_ata_lba_high_exp(register_fis, 0);
}

/**
 * @brief This method simply translates the 64-bit logical block address
 *        field from the SCSI CDB (16 byte) into the ATA task
 *        file (register FIS).  The 2 most significant bytes must be 0,
 *        since ATA devices can, at most, support 48-bits of LBA.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command()
 *
 * @return Indicate if the LBA translation succeeded.
 * @return SATI_SUCCESS This is returned if translation was successful.
 * @return SATI_FAILURE_CHECK_RESPONSE_DATA This is returned if either
 *         of the 2 most significant bytes contain a non-zero value.
 */
SATI_STATUS sati_move_translate_64_bit_lba(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   U8 * cdb          = sati_cb_get_cdb_address(scsi_io);
   U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);

   // Ensure we receive a logical block address that is within range of
   // addressibility per the ATA specification (i.e. 48-bit or 28-bit).
   if ( (sati_get_cdb_byte(cdb, 2) == 0) && (sati_get_cdb_byte(cdb, 3) == 0) )
   {
      sati_set_ata_lba_low(register_fis, sati_get_cdb_byte(cdb, 9));
      sati_set_ata_lba_mid(register_fis, sati_get_cdb_byte(cdb, 8));
      sati_set_ata_lba_high(register_fis, sati_get_cdb_byte(cdb, 7));
      sati_set_ata_lba_low_exp(register_fis, sati_get_cdb_byte(cdb, 6));
      sati_set_ata_lba_mid_exp(register_fis, sati_get_cdb_byte(cdb, 5));
      sati_set_ata_lba_high_exp(register_fis, sati_get_cdb_byte(cdb, 4));
      return SATI_SUCCESS;
   }
   else
   {
      sati_scsi_sense_data_construct(
         sequence,
         scsi_io,
         SCSI_STATUS_CHECK_CONDITION,
         SCSI_SENSE_ILLEGAL_REQUEST,
         SCSI_ASC_LBA_OUT_OF_RANGE,
         SCSI_ASCQ_LBA_OUT_OF_RANGE
      );
      return SATI_FAILURE_CHECK_RESPONSE_DATA;
   }
}

/**
 * @brief This method will translate the pieces common to SCSI read and
 *        write 6 byte commands.  Depending upon the capabilities
 *        supported by the target different ATA commands can be selected.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @return Indicate if the command translation succeeded.
 * @retval SCI_SUCCESS This is returned if the command translation was
 *         successful.
 */
SATI_STATUS sati_move_6_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   U8 * cdb          = sati_cb_get_cdb_address(scsi_io);
   U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);

   // Translate the logical block address information from the SCSI CDB.
   // There is only 5 bits of MSB located in byte 1 of the CDB.
   sati_set_ata_lba_low(register_fis, sati_get_cdb_byte(cdb, 3));
   sati_set_ata_lba_mid(register_fis, sati_get_cdb_byte(cdb, 2));
   sati_set_ata_lba_high(register_fis, sati_get_cdb_byte(cdb, 1) & 0x1F);

   sati_move_translate_command(sequence, scsi_io, ata_io, 0);

   return SATI_SUCCESS;
}

/**
 * @brief This method will translate the pieces common to SCSI read and
 *        write 10/12 byte commands.  Depending upon the capabilities
 *        supported by the target different ATA commands can be selected.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @param[in] device_head This parameter specifies the contents to be
 *            written to the device head register.
 *
 * @return Indicate if the command translation succeeded.
 * @retval SCI_SUCCESS This is returned if the command translation was
 *         successful.
 */
SATI_STATUS sati_move_32_bit_lba_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io,
   U8                           device_head
)
{
   sati_move_translate_32_bit_lba(sequence, scsi_io, ata_io);
   sati_move_translate_command(sequence, scsi_io, ata_io, device_head);

   return SATI_SUCCESS;
}

/**
 * @brief This method provides the common translation functionality for
 *        the 6-byte move command descriptor blocks (CDBs).
 *        This method will ensure that the following is performed:
 *        - command register is set
 *        - the SATI_TRANSLATOR_SEQUENCE::protocol field is set
 *        - the sector count field(s) are set
 *        - sati_move_6_translate_command() is invoked.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @pre The caller must ensure that the
 *      SATI_TRANSLATOR_SEQUENCE::data_direction field has already been set.
 *
 * @return Indicate if the command translation succeeded.
 * @see sati_move_6_translate_command() for additional return codes.
 */
SATI_STATUS sati_move_small_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   // Translation of the sector count is performed differently for NCQ vs.
   // other protocols.
   if (sequence->device->capabilities & SATI_DEVICE_CAP_NCQ_SUPPORTED_ENABLE)
   {
      sati_move_set_ata_command(
         sequence, ata_io, ATA_WRITE_FPDMA, ATA_READ_FPDMA
      );
      sati_move_ncq_translate_8_bit_sector_count(scsi_io, ata_io);
      sequence->protocol = SAT_PROTOCOL_FPDMA;
   }
   else if (sequence->device->capabilities & SATI_DEVICE_CAP_48BIT_ENABLE)
   {
      U8 * cdb = sati_cb_get_cdb_address(scsi_io);

      sati_move_small_udma_translate_command(
         sequence, scsi_io, ata_io, ATA_WRITE_DMA_EXT, ATA_READ_DMA_EXT
      );

      // A read/write 6 with a 0 sector count indicates a transfer of 256
      // sectors.  As a result update the MSB (features expanded register)
      // to indicate 256 sectors (0x100).
      if (sati_get_cdb_byte(cdb, 4) == 0)
      {
         U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);
         sati_set_ata_sector_count_exp(register_fis, 1);
      }
   }
   else if (sequence->device->capabilities & SATI_DEVICE_CAP_UDMA_ENABLE)
   {
      sati_move_small_udma_translate_command(
         sequence, scsi_io, ata_io, ATA_WRITE_DMA, ATA_READ_DMA
      );
   }
   else
   {
      /**
       * Currently the translation does not support devices incapable of
       * handling the 48-bit feature set (i.e. 16 bits of sector count).
       */
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

   return sati_move_6_translate_command(sequence, scsi_io, ata_io);
}

/**
 * @brief This method provides the common translation functionality for
 *        the larger command descriptor blocks (10, 12, 16-byte CDBs).
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @param[in] sector_count This parameter specifies the number of sectors
 *            to be transferred.
 * @param[in] device_head This parameter specifies the contents to be
 *            written to the device head register.
 *
 * @return Indicate if the command translation succeeded.
 * @retval SATI_FAILURE This value is returned if neither NCQ or DMA is
 *         supported by the target device.
 * @see sati_move_set_sector_count() for additional return codes.
 */
SATI_STATUS sati_move_large_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io,
   U32                          sector_count,
   U8                         * ata_device_head
)
{
   SATI_STATUS   status = SATI_SUCCESS;
   U8          * cdb    = sati_cb_get_cdb_address(scsi_io);

   // Parts of translation (e.g. sector count) is performed differently
   // for NCQ vs. other protocols.
   if (sequence->device->capabilities & SATI_DEVICE_CAP_NCQ_SUPPORTED_ENABLE)
   {
      // if the user did not request to ignore FUA
      if((sequence->device->capabilities & SATI_DEVICE_CAP_IGNORE_FUA)==0)
      {
         // Is the Force Unit Access bit set?
         if (sati_get_cdb_byte(cdb, 1) & SCSI_MOVE_FUA_BIT_ENABLE)
            *ata_device_head = ATA_DEV_HEAD_REG_FUA_ENABLE;
      }

      sati_move_set_ata_command(
         sequence, ata_io, ATA_WRITE_FPDMA, ATA_READ_FPDMA
      );
      status = sati_move_set_sector_count(
                  sequence, scsi_io, ata_io, sector_count, TRUE
               );
      sequence->protocol = SAT_PROTOCOL_FPDMA;
   }
   else if (sequence->device->capabilities & SATI_DEVICE_CAP_48BIT_ENABLE)
   {
      // Is the Force Unit Access bit set?  If it is, then error.  We
      // aren't supporting this yet for normal DMA.
      if (sati_get_cdb_byte(cdb, 1) & SCSI_MOVE_FUA_BIT_ENABLE)
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

      status = sati_move_large_udma_translate_command(
                  sequence,
                  scsi_io,
                  ata_io,
                  sector_count,
                  ATA_WRITE_DMA_EXT,
                  ATA_READ_DMA_EXT
               );
   }
   else if (sequence->device->capabilities & SATI_DEVICE_CAP_UDMA_ENABLE)
   {
      status = sati_move_large_udma_translate_command(
                  sequence,
                  scsi_io,
                  ata_io,
                  sector_count,
                  ATA_WRITE_DMA,
                  ATA_READ_DMA
               );
   }
   else
   {
      /**
       * Currently the translation does not support devices incapable of
       * handling the 48-bit feature set (i.e. 16 bits of sector count).
       */
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

   return status;
}

/**
 * @brief This method simply performs the functionality common to all
 *        payload data movement translations (i.e. READ/WRITE).
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @param[in] device_head This parameter specifies the current contents
 *            to be written to the ATA task file (register FIS) device
 *            head register.
 *
 * @return none
 */
void sati_move_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io,
   U8                           device_head
)
{
   U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);

   sati_set_ata_device_head(
      register_fis, device_head | ATA_DEV_HEAD_REG_LBA_MODE_ENABLE
   );
}

