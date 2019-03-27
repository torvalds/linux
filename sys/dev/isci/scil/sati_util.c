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
 *        provide generic support for SATI.  Some methods can be utilized
 *        by a user to construct ATA/ATAPI commands, copy ATA device
 *        structure data, fill in sense data, etc.
 */

#include <dev/isci/scil/sati_util.h>
#include <dev/isci/scil/sati_callbacks.h>
#include <dev/isci/scil/intel_scsi.h>
#include <dev/isci/scil/intel_ata.h>
#include <dev/isci/scil/intel_sat.h>
#include <dev/isci/scil/intel_sas.h>

/**
 * @brief This method will set the data direction, protocol, and transfer
 *        kength for an ATA non-data command.
 *
 * @pre It is expected that the user will use this method for setting these
 *      values in a non-data ATA command constuct.
 *
 * @param[out] ata_io This parameter specifies the ATA IO request structure
 *             for which to build the IDENTIFY DEVICE command.
 * @param[in]  sequence This parameter specifies the translator sequence
 *             for which the command is being constructed.
 *
 * @return none.
 */
void sati_ata_non_data_command(
   void                        * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T  * sequence
)
{
   sequence->data_direction      = SATI_DATA_DIRECTION_NONE;
   sequence->protocol            = SAT_PROTOCOL_NON_DATA;
   sequence->ata_transfer_length = 0;
}

/**
 * @brief This method will construct the ATA identify device command.
 *
 * @pre It is expected that the user has properly set the current contents
 *      of the register FIS to 0.
 *
 * @param[out] ata_io This parameter specifies the ATA IO request structure
 *             for which to build the IDENTIFY DEVICE command.
 * @param[in]  sequence This parameter specifies the translator sequence
 *             for which the command is being constructed.
 *
 * @return none.
 */
void sati_ata_identify_device_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence
)
{
   U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);

   sati_set_ata_command(register_fis, ATA_IDENTIFY_DEVICE);
   sequence->data_direction      = SATI_DATA_DIRECTION_IN;
   sequence->protocol            = SAT_PROTOCOL_PIO_DATA_IN;
   sequence->ata_transfer_length = sizeof(ATA_IDENTIFY_DEVICE_DATA_T);
}

/**
* @brief This method will construct the ATA Execute Device Diagnostic command.
*
* @param[out] ata_io This parameter specifies the ATA IO request structure
*             for which to build the IDENTIFY DEVICE command.
* @param[in]  sequence This parameter specifies the translator sequence
*             for which the command is being constructed.
*
* @return none.
*/
void sati_ata_execute_device_diagnostic_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence
)
{
   U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);

   sati_set_ata_command(register_fis, ATA_EXECUTE_DEVICE_DIAG);

   sequence->data_direction = SATI_DATA_DIRECTION_IN;
   sequence->protocol = SAT_PROTOCOL_DEVICE_DIAGNOSTIC;
   sequence->ata_transfer_length = 16;
}

/**
 * @brief This method will set data bytes in the user data area.  If the
 *        caller requests it, the data written will be forced to ascii
 *        printable characters if it isn't already a printable character.
 *        A printable character is considered to be >= 0x20 and <= 0x70.
 *
 * @param[in]  sequence This parameter specifies the translation sequence
 *             for which to copy and swap the data.
 * @param[out] destination_scsi_io This parameter specifies the SCSI IO
 *             request containing the destination buffer into which to copy.
 * @param[in]  destination_offset This parameter specifies the offset into
 *             the data buffer where the information will be copied to.
 * @param[in]  source_value This parameter specifies the value retrieved
 *             from the source buffer that is to be copied into the user
 *             buffer area.
 * @param[in]  use_printable_chars This parameter indicates if the copy should
 *             ensure that the value copied is considered an ASCII printable
 *             character (e.g. A, B, " ", etc.).  These characters reside
 *             in the 0x20 - 0x7E ASCII range.
 *
 * @return none
 */
static
void sati_set_ascii_data_byte(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * destination_scsi_io,
   U32                          destination_offset,
   U8                           source_value,
   BOOL                         use_printable_chars
)
{
   // if the user requests that the copied data be ascii printable, then
   // default to " " (i.e. 0x20) for all non-ascii printable characters.
   if((use_printable_chars == TRUE)
     && ((source_value < 0x20) || (source_value > 0x7E)))
   {
      source_value = 0x20;
   }

   sati_set_data_byte(
      sequence, destination_scsi_io, destination_offset, source_value
   );
}

/**
 * @brief This method performs a copy operation using an offset into the
 *        source buffer, an offset into the destination buffer, and a length.
 *        It will perform the byte swap from the 16-bit identify field
 *        into the network byte order SCSI location.
 *
 * @param[in]  sequence This parameter specifies the translation sequence
 *             for which to copy and swap the data.
 * @param[out] destination_scsi_io This parameter specifies the SCSI IO
 *             request containing the destination buffer into which to copy.
 * @param[in]  destination_offset This parameter specifies the offset into
 *             the data buffer where the information will be copied to.
 * @param[in]  source_buffer This parameter specifies the source buffer from
 *             which the data will be copied.
 * @param[in]  source_offset This parameter specifies the offset into the
 *             source buffer where the copy shall begin.
 * @param[in]  length This parameter specifies the number of bytes to copy
 *             during this operation.
 * @param[in]  use_printable_chars This parameter indicates if the copy should
 *             ensure that the value copied is considered an ASCII printable
 *             character (e.g. A, B, " ", etc.).  These characters reside
 *             in the 0x20 - 0x7E ASCII range.
 *
 * @return none
 */
void sati_ata_identify_device_copy_data(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * destination_scsi_io,
   U32                          destination_offset,
   U8                         * source_buffer,
   U32                          source_offset,
   U32                          length,
   BOOL                         use_printable_chars
)
{
   source_buffer += source_offset;
   while (length > 0)
   {
      sati_set_ascii_data_byte(
         sequence,
         destination_scsi_io,
         destination_offset,
         *(source_buffer+1),
         use_printable_chars
      );

      sati_set_ascii_data_byte(
         sequence,
         destination_scsi_io,
         destination_offset+1,
         *source_buffer,
         use_printable_chars
      );

      destination_offset += 2;
      source_buffer      += 2;
      length             -= 2;
   }
}

/**
 * @brief This method performs a copy operation using a source buffer,
 *        an offset into the destination buffer, and a length.
 *
 * @param[in]  sequence This parameter specifies the translation sequence
 *             for which to copy and swap the data.
 * @param[out] destination_scsi_io This parameter specifies the SCSI IO
 *             request containing the destination buffer into which to copy.
 * @param[in]  destination_offset This parameter specifies the offset into
 *             the data buffer where the information will be copied to.
 * @param[in]  source_buffer This parameter specifies the source buffer from
 *             which the data will be copied.
 * @param[in]  length This parameter specifies the number of bytes to copy
 *             during this operation.
 *
 * @return none
 */
void sati_copy_data(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * destination_scsi_io,
   U32                          destination_offset,
   U8                         * source_buffer,
   U32                          length
)
{
   while (length > 0)
   {
      sati_set_data_byte(
         sequence, destination_scsi_io, destination_offset, *source_buffer
      );

      destination_offset++;
      source_buffer++;
      length--;
   }
}

/**
 * @brief This method extracts the Logical Block Address high and low 32-bit
 *        values and the sector count 32-bit value from the ATA identify
 *        device data.
 *
 * @param[in]  identify This parameter specifies the ATA_IDENTIFY_DEVICE_DATA
 *             from which to extract the sector information.
 * @param[out] lba_high This parameter specifies the upper 32 bits for the
 *             number of logical block addresses for the device. The upper
 *             16-bits should always be 0, since 48-bits of LBA is the most
 *             supported by an ATA device.
 * @param[out] lba_low This parameter specifies the lower 32 bits for the
 *             number of logical block addresses for the device.
 * @param[out] sector_size This parameter specifies the 32-bits of sector
 *             size.  If the ATA device doesn't support reporting it's
 *             sector size, then 512 bytes is utilized as the default value.
 *
 * @return none
 */
void sati_ata_identify_device_get_sector_info(
   ATA_IDENTIFY_DEVICE_DATA_T * identify,
   U32                        * lba_high,
   U32                        * lba_low,
   U32                        * sector_size
)
{
   // Calculate the values to be returned
   // Calculation will be different if the SATA device supports
   // 48-bit addressing.  Bit 10 of Word 86 of ATA Identify
   if (identify->command_set_enabled1
       & ATA_IDENTIFY_COMMAND_SET_SUPPORTED1_48BIT_ENABLE)
   {
      // This drive supports 48-bit addressing

      *lba_high  = identify->max_48bit_lba[7] << 24;
      *lba_high |= identify->max_48bit_lba[6] << 16;
      *lba_high |= identify->max_48bit_lba[5] << 8;
      *lba_high |= identify->max_48bit_lba[4];

      *lba_low  = identify->max_48bit_lba[3] << 24;
      *lba_low |= identify->max_48bit_lba[2] << 16;
      *lba_low |= identify->max_48bit_lba[1] << 8;
      *lba_low |= identify->max_48bit_lba[0];
   }
   else
   {
      // This device doesn't support 48-bit addressing
      // Pull out the largest LBA from words 60 and 61.
      *lba_high  = 0;
      *lba_low   = identify->total_num_sectors[3] << 24;
      *lba_low  |= identify->total_num_sectors[2] << 16;
      *lba_low  |= identify->total_num_sectors[1] << 8;
      *lba_low  |= identify->total_num_sectors[0];
   }

   // If the ATA device reports its sector size (bit 12 of Word 106),
   // then use that instead.
   if (identify->physical_logical_sector_info
       & ATA_IDENTIFY_SECTOR_LARGER_THEN_512_ENABLE)
   {
      *sector_size  = identify->words_per_logical_sector[3] << 24;
      *sector_size |= identify->words_per_logical_sector[2] << 16;
      *sector_size |= identify->words_per_logical_sector[1] << 8;
      *sector_size |= identify->words_per_logical_sector[0];
   }
   else
   {
      // Default the sector size to 512 bytes
      *sector_size = 512;
   }
}

/**
 * @brief This method will construct the ATA check power mode command.
 *
 * @pre It is expected that the user has properly set the current contents
 *      of the register FIS to 0.
 *
 * @param[out] ata_io This parameter specifies the ATA IO request structure
 *             for which to build the CHECK POWER MODE command.
 * @param[in]  sequence This parameter specifies the translator sequence
 *             for which the command is being constructed.
 *
 * @return none.
 */
void sati_ata_check_power_mode_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence
)
{
   U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);

   sati_set_ata_command(register_fis, ATA_CHECK_POWER_MODE);
   sati_ata_non_data_command(ata_io, sequence);
}

/**
 * @brief This method is utilized to set a specific byte in the sense
 *        data area.  It will ensure that the supplied byte offset
 *        isn't larger then the length of the requested sense data.
 *
 * @param[in] scsi_io This parameter specifies the user SCSI IO request
 *            for which to set the sense data byte.
 * @param[in] byte_offset This parameter specifies the byte offset into
 *            the sense data buffer where the data should be written.
 * @param[in] value This parameter specifies the 8-bit value to be written
 *            into the sense data area.
 *
 * @return none
 */
void sati_set_sense_data_byte(
   U8  * sense_data,
   U32   max_sense_data_len,
   U32   byte_offset,
   U8    value
)
{
   // Ensure that we don't attempt to write past the end of the sense
   // data buffer.
   if (byte_offset < max_sense_data_len)
      sense_data[byte_offset] = value;
}

/**
 * @brief This method will construct the common response IU in the user
 *           request's response IU location.
 *
 * @param[out] rsp_iu This parameter specifies the user request's
 *                response IU to be constructed.
 * @param[in]  scsi_status This parameter specifies the SCSI status
 *                value for the user's IO request.
 * @param[in]  sense_data_length This parameter specifies the sense data
 *                length for response IU.
 * @param[in]  data_present The parameter specifies the specific
 *                data present value for response IU.
 *
 * @return none
 */
void sati_scsi_common_response_iu_construct(
   SCI_SSP_RESPONSE_IU_T * rsp_iu,
   U8                      scsi_status,
   U8                      sense_data_length,
   U8                      data_present
)
{
   rsp_iu->sense_data_length[3] = sense_data_length;
   rsp_iu->sense_data_length[2] = 0;
   rsp_iu->sense_data_length[1] = 0;
   rsp_iu->sense_data_length[0] = 0;
   rsp_iu->status               = scsi_status;
   rsp_iu->data_present         = data_present;
}

/**
 * @brief This method will construct the buffer for sense data
 *        sense data buffer location.  Additionally, it will set the user's
 *        SCSI status.
 *
 * @param[in,out] scsi_io This parameter specifies the user's IO request
 *                for which to construct the buffer for sense data.
 * @param[in]     scsi_status This parameter specifies the SCSI status
 *                value for the user's IO request.
 * @param[out]    sense_data This paramater
 *
 * @return none
 */
static
void sati_scsi_get_sense_data_buffer(
    SATI_TRANSLATOR_SEQUENCE_T      * sequence,
    void                            * scsi_io,
    U8                                scsi_status,
    U8                             ** sense_data,
    U32                             * sense_len)
{
#ifdef SATI_TRANSPORT_SUPPORTS_SAS
   SCI_SSP_RESPONSE_IU_T * rsp_iu = (SCI_SSP_RESPONSE_IU_T*)
                                    sati_cb_get_response_iu_address(scsi_io);

   sati_scsi_common_response_iu_construct(
      rsp_iu,
      scsi_status,
      sati_scsi_get_sense_data_length(sequence, scsi_io),
      SCSI_RESPONSE_DATA_PRES_SENSE_DATA
   );

   *sense_data                   = (U8*) rsp_iu->data;
   *sense_len                    = SSP_RESPONSE_IU_MAX_DATA * 4;  // dwords to bytes
#else
   *sense_data = sati_cb_get_sense_data_address(scsi_io);
   *sense_len  = sati_cb_get_sense_data_length(scsi_io);
   sati_cb_set_scsi_status(scsi_io, scsi_status);
#endif // SATI_TRANSPORT_SUPPORTS_SAS
}

/**
 * @brief This method extract response code based on on device settings.
 *
 * @return response code
 */
static
U8 sati_scsi_get_sense_data_response_code(SATI_TRANSLATOR_SEQUENCE_T * sequence)
{
    if (sequence->device->descriptor_sense_enable)
    {
       return SCSI_DESCRIPTOR_CURRENT_RESPONSE_CODE;
    }
    else
    {
       return SCSI_FIXED_CURRENT_RESPONSE_CODE;
    }
}

/**
 * @brief This method will return length of descriptor sense data for executed command.
 *
 * @return sense data length
 */
static
U8 sati_scsi_get_descriptor_sense_data_length(SATI_TRANSLATOR_SEQUENCE_T * sequence,
        void * scsi_io)
{
    U8 * cdb = sati_cb_get_cdb_address(scsi_io);
    //Initial value is descriptor header length
    U8 length = 8;

    switch (sati_get_cdb_byte(cdb, 0))
    {
#if !defined(DISABLE_SATI_WRITE_LONG)
    case SCSI_WRITE_LONG_10:
    case SCSI_WRITE_LONG_16:
        length += SCSI_BLOCK_DESCRIPTOR_LENGTH +
            SCSI_INFORMATION_DESCRIPTOR_LENGTH;
        break;
#endif // !defined(DISABLE_SATI_WRITE_LONG)
#if !defined(DISABLE_SATI_REASSIGN_BLOCKS)
    case SCSI_REASSIGN_BLOCKS:
        length += SCSI_CMD_SPECIFIC_DESCRIPTOR_LENGTH +
            SCSI_INFORMATION_DESCRIPTOR_LENGTH;
        break;
#endif // !defined(DISABLE_SATI_REASSIGN_BLOCKS)
    case SCSI_READ_6:
    case SCSI_READ_10:
    case SCSI_READ_12:
    case SCSI_READ_16:
    case SCSI_WRITE_6:
    case SCSI_WRITE_10:
    case SCSI_WRITE_12:
    case SCSI_WRITE_16:
#if !defined(DISABLE_SATI_VERIFY)
    case SCSI_VERIFY_10:
    case SCSI_VERIFY_12:
    case SCSI_VERIFY_16:
#endif // !defined(DISABLE_SATI_VERIFY)
#if    !defined(DISABLE_SATI_WRITE_AND_VERIFY)  \
    && !defined(DISABLE_SATI_VERIFY)            \
    && !defined(DISABLE_SATI_WRITE)

    case SCSI_WRITE_AND_VERIFY_10:
    case SCSI_WRITE_AND_VERIFY_12:
    case SCSI_WRITE_AND_VERIFY_16:
#endif //    !defined(DISABLE_SATI_WRITE_AND_VERIFY)
       // && !defined(DISABLE_SATI_VERIFY)
       // && !defined(DISABLE_SATI_WRITE)
        length += SCSI_INFORMATION_DESCRIPTOR_LENGTH;
        break;
    }

    return length;
}

/**
 * @brief This method will return length of sense data.
 *
 * @return sense data length
 */
U8 sati_scsi_get_sense_data_length(SATI_TRANSLATOR_SEQUENCE_T * sequence, void * scsi_io)
{
    U8 response_code;

    response_code = sati_scsi_get_sense_data_response_code(sequence);

    switch (response_code)
    {
    case SCSI_FIXED_CURRENT_RESPONSE_CODE:
    case SCSI_FIXED_DEFERRED_RESPONSE_CODE:
        return SCSI_FIXED_SENSE_DATA_BASE_LENGTH;
    break;
    case SCSI_DESCRIPTOR_CURRENT_RESPONSE_CODE:
    case SCSI_DESCRIPTOR_DEFERRED_RESPONSE_CODE:
        return sati_scsi_get_descriptor_sense_data_length(sequence, scsi_io);
    break;
    }

    return SCSI_FIXED_SENSE_DATA_BASE_LENGTH;
}

/**
 * @brief This method will construct the sense data buffer in the user's
 *        sense data buffer location.  Additionally, it will set the user's
 *        SCSI status.
 *
 * @param[in]     sequence This parameter specifies the translation sequence
 *                for which to construct the sense data.
 * @param[in,out] scsi_io This parameter specifies the user's IO request
 *                for which to construct the sense data.
 * @param[in]     scsi_status This parameter specifies the SCSI status
 *                value for the user's IO request.
 * @param[in]     sense_key This parameter specifies the sense key to
 *                be set for the user's IO request.
 * @param[in]     additional_sense_code This parameter specifies the
 *                additional sense code (ASC) key to be set for the user's
 *                IO request.
 * @param[in]     additional_sense_code_qualifier This parameter specifies
 *                the additional sense code qualifier (ASCQ) key to be set
 *                for the user's IO request.
 *
 * @return none
 */
void sati_scsi_sense_data_construct(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   U8                           scsi_status,
   U8                           sense_key,
   U8                           additional_sense_code,
   U8                           additional_sense_code_qualifier
)
{
    U8 response_code;

    response_code = sati_scsi_get_sense_data_response_code(sequence);

    switch (response_code)
    {
    case SCSI_FIXED_CURRENT_RESPONSE_CODE:
    case SCSI_FIXED_DEFERRED_RESPONSE_CODE:
    sati_scsi_fixed_sense_data_construct(sequence, scsi_io, scsi_status, response_code,
                sense_key, additional_sense_code, additional_sense_code_qualifier);
    break;
    case SCSI_DESCRIPTOR_CURRENT_RESPONSE_CODE:
    case SCSI_DESCRIPTOR_DEFERRED_RESPONSE_CODE:
        sati_scsi_descriptor_sense_data_construct(sequence, scsi_io, scsi_status, response_code,
                sense_key, additional_sense_code, additional_sense_code_qualifier);
        break;
    }

    sequence->is_sense_response_set = TRUE;
}

/**
 * @brief This method will construct the block descriptor in the user's descriptor
 *            sense data buffer location.
 *
 * @param[in]     sense_data This parameter specifies the user SCSI IO request
 *                for which to set the sense data byte.
 * @param[in]     sense_len This parameter specifies length of the sense data
 *                to be returned by SATI.
 * @param[out]    descriptor_len This parameter returns the length of constructed
 *                descriptor.
 *
 * @return none
 */
static
void sati_scsi_block_descriptor_construct(
        U8 * sense_data,
        U32 sense_len)
{
    U8 ili = 1;

    sati_set_sense_data_byte(sense_data, sense_len, 0,  SCSI_BLOCK_DESCRIPTOR_TYPE);
    sati_set_sense_data_byte(sense_data, sense_len, 1,  SCSI_BLOCK_DESCRIPTOR_ADDITIONAL_LENGTH);
    sati_set_sense_data_byte(sense_data, sense_len, 2,  0);
    sati_set_sense_data_byte(sense_data, sense_len, 3,  (ili << 5));
}

/**
 * @brief This method will construct the command-specific descriptor for
 *           the descriptor sense data buffer in the user's sense data buffer
 *           location.
 *
 * @param[in]     sense_data This parameter specifies the user SCSI IO request
 *                for which to set the sense data byte.
 * @param[in]     sense_len This parameter specifies length of the sense data
 *                to be returned by SATI.
 * @param[out]    descriptor_len This parameter returns the length of constructed
 *                descriptor.
 * @param[in]     information_buff This parameter specifies the address for which
 *                to set the command-specific information buffer.
 *
 * @return none
 */
static
void sati_scsi_command_specific_descriptor_construct(
    U8       * sense_data,
    U32        sense_len,
    U8       * information_buff)
{
    U8 i;

    sati_set_sense_data_byte(sense_data, sense_len, 0,  SCSI_CMD_SPECIFIC_DESCRIPTOR_TYPE);
    sati_set_sense_data_byte(sense_data, sense_len, 1,  SCSI_CMD_SPECIFIC_DESCRIPTOR_ADDITIONAL_LENGTH);
    sati_set_sense_data_byte(sense_data, sense_len, 2,  0);
    sati_set_sense_data_byte(sense_data, sense_len, 3,  0);

    // fill information buffer
    // SBC 5.20.1 REASSIGN BLOCKS command overview
    // If information about the first LBA not reassigned is not available
    // COMMAND-SPECIFIC INFORMATION field shall be set to FFFF_FFFF_FFFF_FFFFh
    for (i=0; i<8; i++)
        sati_set_sense_data_byte(sense_data, sense_len, 4 + i, information_buff==NULL?0xFF:information_buff[i]);
}

/**
 * @brief This method will construct the information descriptor for
 *           the descriptor sense data buffer in the user's sense data buffer
 *           location.
 *
 * @param[in]     sense_data This parameter specifies the user SCSI IO request
 *                for which to set the sense data byte.
 * @param[in]     sense_len This parameter specifies length of the sense data
 *                to be returned by SATI.
 * @param[out]    descriptor_len This parameter returns the length of constructed
 *                descriptor.
 * @param[in]     information_buff This parameter specifies the address for which
 *                to set the information buffer.
 *
 * @return none
 */
static
void sati_scsi_information_descriptor_construct(
    U8      * sense_data,
    U32       sense_len,
    U8      * information_buff)
{
    U8 i;
    U8 valid = 1;

    sati_set_sense_data_byte(sense_data, sense_len, 0,  SCSI_INFORMATION_DESCRIPTOR_TYPE);
    sati_set_sense_data_byte(sense_data, sense_len, 1,  SCSI_INFORMATION_DESCRIPTOR_ADDITIONAL_LENGTH);
    sati_set_sense_data_byte(sense_data, sense_len, 2,  (valid << 7));
    sati_set_sense_data_byte(sense_data, sense_len, 3,  0);

    // fill information buffer
    for (i=0; i<8; i++)
        sati_set_sense_data_byte(sense_data, sense_len, 4 + i, information_buff==NULL?0:information_buff[i]);
}

/**
 * @brief This method will construct the descriptors in the user's descriptor
 *           sense data buffer location.
 *
 * @param[in,out] scsi_io This parameter specifies the user's IO request
 *                for which to construct the sense data.
 * @param[in]     sense_data This parameter specifies the user SCSI IO request
 *                for which to set the sense data byte.
 * @param[in]     sense_len This parameter specifies length of the sense data
 *                to be returned by SATI.
 * @param[out]    descriptor_len This parameter returns the length of constructed
 *                descriptor.
 * @param[in]     information_buff This parameter specifies the address for which
 *                to set the information buffer.
 *
 * @return none
 */
static
void sati_scsi_common_descriptors_construct(
    void    * scsi_io,
    U8      * sense_data,
    U32       sense_len,
    U8      * information_buff)
{
    U8 * cdb = sati_cb_get_cdb_address(scsi_io);
    U8 offset = 0;

    switch (sati_get_cdb_byte(cdb, 0))
    {
#if !defined(DISABLE_SATI_WRITE_LONG)
    case SCSI_WRITE_LONG_10:
    case SCSI_WRITE_LONG_16:
        sati_scsi_block_descriptor_construct(
                sense_data + offset,
                sense_len - offset);

        offset += SCSI_BLOCK_DESCRIPTOR_LENGTH;
        sati_scsi_information_descriptor_construct(
                  sense_data + offset,
                  sense_len - offset,
                  information_buff);

        offset += SCSI_INFORMATION_DESCRIPTOR_LENGTH;
        break;
#endif // !defined(DISABLE_SATI_WRITE_LONG)
#if !defined(DISABLE_SATI_REASSIGN_BLOCKS)
    case SCSI_REASSIGN_BLOCKS:
        sati_scsi_command_specific_descriptor_construct(
          sense_data + offset,
          sense_len - offset,
          NULL);

        offset += SCSI_CMD_SPECIFIC_DESCRIPTOR_LENGTH;
        sati_scsi_information_descriptor_construct(
                  sense_data + offset,
                  sense_len - offset,
                  information_buff);

        offset += SCSI_INFORMATION_DESCRIPTOR_LENGTH;
        break;
#endif // !defined(DISABLE_SATI_REASSIGN_BLOCKS)
    case SCSI_READ_6:
    case SCSI_READ_10:
    case SCSI_READ_12:
    case SCSI_READ_16:
    case SCSI_WRITE_6:
    case SCSI_WRITE_10:
    case SCSI_WRITE_12:
    case SCSI_WRITE_16:
#if !defined(DISABLE_SATI_VERIFY)
    case SCSI_VERIFY_10:
    case SCSI_VERIFY_12:
    case SCSI_VERIFY_16:
#endif // !defined(DISABLE_SATI_VERIFY)
#if    !defined(DISABLE_SATI_WRITE_AND_VERIFY)  \
    && !defined(DISABLE_SATI_VERIFY)            \
    && !defined(DISABLE_SATI_WRITE)

    case SCSI_WRITE_AND_VERIFY_10:
    case SCSI_WRITE_AND_VERIFY_12:
    case SCSI_WRITE_AND_VERIFY_16:
#endif //    !defined(DISABLE_SATI_WRITE_AND_VERIFY)
       // && !defined(DISABLE_SATI_VERIFY)
       // && !defined(DISABLE_SATI_WRITE)
        sati_scsi_information_descriptor_construct(
                  sense_data + offset,
                  sense_len - offset,
                  information_buff);

        offset += SCSI_INFORMATION_DESCRIPTOR_LENGTH;
        break;
    }
}

/**
 * @brief This method will construct the descriptor sense data buffer in
 *           the user's sense data buffer location.  Additionally, it will set
 *           the user's SCSI status.
 *
 * @param[in]     sequence This parameter specifies the translation sequence
 *                for which to construct the sense data.
 * @param[in,out] scsi_io This parameter specifies the user's IO request
 *                for which to construct the sense data.
 * @param[in]     scsi_status This parameter specifies the SCSI status
 *                value for the user's IO request.
 * @param[in]     sense_key This parameter specifies the sense key to
 *                be set for the user's IO request.
 * @param[in]     additional_sense_code This parameter specifies the
 *                additional sense code (ASC) key to be set for the user's
 *                IO request.
 * @param[in]     additional_sense_code_qualifier This parameter specifies
 *                the additional sense code qualifier (ASCQ) key to be set
 *                for the user's IO request.
 *
 * @return none
 */
void sati_scsi_descriptor_sense_data_construct(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   U8                           scsi_status,
   U8                           response_code,
   U8                           sense_key,
   U8                           additional_sense_code,
   U8                           additional_sense_code_qualifier
)
{
   U8 * sense_data;
   U32    sense_len;

   sati_scsi_get_sense_data_buffer(sequence, scsi_io, scsi_status, &sense_data, &sense_len);

   sati_set_sense_data_byte(
      sense_data,
      sense_len,
      0,
      response_code
   );

   sati_set_sense_data_byte(sense_data, sense_len, 1,  sense_key);
   sati_set_sense_data_byte(sense_data, sense_len, 2,  additional_sense_code);
   sati_set_sense_data_byte(sense_data, sense_len, 3,  additional_sense_code_qualifier);
   sati_set_sense_data_byte(sense_data, sense_len, 4,  0);
   sati_set_sense_data_byte(sense_data, sense_len, 5,  0);
   sati_set_sense_data_byte(sense_data, sense_len, 6,  0);

   sati_scsi_common_descriptors_construct(scsi_io, sense_data + 8, sense_len, NULL);

   sati_set_sense_data_byte(sense_data, sense_len, 7,  sati_scsi_get_descriptor_sense_data_length(sequence, scsi_io) - 8);
}

/**
 * @brief This method will construct the fixed format sense data buffer
 *           in the user's sense data buffer location.  Additionally, it will
 *          set the user's SCSI status.
 *
 * @param[in]     sequence This parameter specifies the translation sequence
 *                for which to construct the sense data.
 * @param[in,out] scsi_io This parameter specifies the user's IO request
 *                for which to construct the sense data.
 * @param[in]     scsi_status This parameter specifies the SCSI status
 *                value for the user's IO request.
 * @param[in]     sense_key This parameter specifies the sense key to
 *                be set for the user's IO request.
 * @param[in]     additional_sense_code This parameter specifies the
 *                additional sense code (ASC) key to be set for the user's
 *                IO request.
 * @param[in]     additional_sense_code_qualifier This parameter specifies
 *                the additional sense code qualifier (ASCQ) key to be set
 *                for the user's IO request.
 *
 * @return none
 */
void sati_scsi_fixed_sense_data_construct(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   U8                           scsi_status,
   U8                           response_code,
   U8                           sense_key,
   U8                           additional_sense_code,
   U8                           additional_sense_code_qualifier
)
{
    U8 * sense_data;
    U32  sense_len;

    sati_scsi_get_sense_data_buffer(sequence, scsi_io, scsi_status, &sense_data, &sense_len);

    // Write out the sense data format per SPC-4.
    // We utilize the fixed format sense data format.

    sati_set_sense_data_byte(
      sense_data,
      sense_len,
      0,
      response_code | SCSI_FIXED_SENSE_DATA_VALID_BIT
    );

    sati_set_sense_data_byte(sense_data, sense_len, 1,  0);
    sati_set_sense_data_byte(sense_data, sense_len, 2,  sense_key);
    sati_set_sense_data_byte(sense_data, sense_len, 3,  0);
    sati_set_sense_data_byte(sense_data, sense_len, 4,  0);
    sati_set_sense_data_byte(sense_data, sense_len, 5,  0);
    sati_set_sense_data_byte(sense_data, sense_len, 6,  0);
    sati_set_sense_data_byte(sense_data, sense_len, 7,  (sense_len < 18 ? sense_len - 1 : 17) - 7);
    sati_set_sense_data_byte(sense_data, sense_len, 8,  0);
    sati_set_sense_data_byte(sense_data, sense_len, 9,  0);
    sati_set_sense_data_byte(sense_data, sense_len, 10, 0);
    sati_set_sense_data_byte(sense_data, sense_len, 11, 0);
    sati_set_sense_data_byte(sense_data, sense_len, 12, additional_sense_code);
    sati_set_sense_data_byte(sense_data, sense_len, 13, additional_sense_code_qualifier);
    sati_set_sense_data_byte(sense_data, sense_len, 14, 0);
    sati_set_sense_data_byte(sense_data, sense_len, 15, 0);
    sati_set_sense_data_byte(sense_data, sense_len, 16, 0);
    sati_set_sense_data_byte(sense_data, sense_len, 17, 0);
}

/**
* @brief This method will construct common sense data that will be identical in
*        both read error sense construct functions.
*        sati_scsi_read_ncq_error_sense_construct,
*        sati_scsi_read_error_sense_construct
*
 * @param[in]    sense_data This parameter specifies the user SCSI IO request
 *               for which to set the sense data byte.
* @param[in]     sense_len This parameter specifies length of the sense data
*                to be returned by SATI.
* @param[in]     sense_key This parameter specifies the sense key to
*                be set for the user's IO request.
* @param[in]     additional_sense_code This parameter specifies the
*                additional sense code (ASC) key to be set for the user's
*                IO request.
* @param[in]     additional_sense_code_qualifier This parameter specifies
*                the additional sense code qualifier (ASCQ) key to be set
*                for the user's IO request.
*
* @return none
*/
static
void sati_scsi_common_fixed_sense_construct(
   U8                         * sense_data,
   U32                          sense_len,
   U8                           sense_key,
   U8                           additional_sense_code,
   U8                           additional_sense_code_qualifier
)
{

   sati_set_sense_data_byte(sense_data, sense_len, 1,  0);
   sati_set_sense_data_byte(sense_data, sense_len, 2,  sense_key);

   //Bytes 3, 4, 5, 6 are set in read_error_sense_construct functions

   sati_set_sense_data_byte(sense_data, sense_len, 7,  (sense_len < 18 ? sense_len - 1 : 17) - 7);
   sati_set_sense_data_byte(sense_data, sense_len, 8,  0);
   sati_set_sense_data_byte(sense_data, sense_len, 9,  0);
   sati_set_sense_data_byte(sense_data, sense_len, 10, 0);
   sati_set_sense_data_byte(sense_data, sense_len, 11, 0);
   sati_set_sense_data_byte(sense_data, sense_len, 12, additional_sense_code);
   sati_set_sense_data_byte(sense_data, sense_len, 13, additional_sense_code_qualifier);
   sati_set_sense_data_byte(sense_data, sense_len, 14, 0);
   sati_set_sense_data_byte(sense_data, sense_len, 15, 0x80);
   sati_set_sense_data_byte(sense_data, sense_len, 16, 0);
   sati_set_sense_data_byte(sense_data, sense_len, 17, 0);
}

/**
 * @brief This method will construct the descriptor sense data buffer in
 *           the user's sense data buffer location.  Additionally, it will set
 *           the user's SCSI status.
 *
 * @param[in]     sequence This parameter specifies the translation sequence
 *                for which to construct the sense data.
 * @param[in,out] scsi_io This parameter specifies the user's IO request
 *                for which to construct the sense data.
 * @param[in]     scsi_status This parameter specifies the SCSI status
 *                value for the user's IO request.
 * @param[in]     sense_key This parameter specifies the sense key to
 *                be set for the user's IO request.
 * @param[in]     additional_sense_code This parameter specifies the
 *                additional sense code (ASC) key to be set for the user's
 *                IO request.
 * @param[in]     additional_sense_code_qualifier This parameter specifies
 *                the additional sense code qualifier (ASCQ) key to be set
 *                for the user's IO request.
 *
 * @return none
 */
static
void sati_scsi_common_descriptor_sense_construct(
    SATI_TRANSLATOR_SEQUENCE_T * sequence,
    void                       * scsi_io,
    U8                         * sense_data,
    U32                          sense_len,
    U8                           sense_key,
    U8                           additional_sense_code,
    U8                           additional_sense_code_qualifier,
    U8                         * information_buff
)
{
    sati_set_sense_data_byte(sense_data, sense_len, 1,  sense_key);
    sati_set_sense_data_byte(sense_data, sense_len, 2,  additional_sense_code);
    sati_set_sense_data_byte(sense_data, sense_len, 3,  additional_sense_code_qualifier);
    sati_set_sense_data_byte(sense_data, sense_len, 4,  0);
    sati_set_sense_data_byte(sense_data, sense_len, 5,  0);
    sati_set_sense_data_byte(sense_data, sense_len, 6,  0);

    sati_scsi_common_descriptors_construct(scsi_io, sense_data + 8, sense_len, information_buff);

    sati_set_sense_data_byte(sense_data, sense_len, 7,  sati_scsi_get_descriptor_sense_data_length(sequence, scsi_io) - 8);
}

/**
* @brief This method will construct the sense data buffer in the user's
*        descriptor sense data buffer location.  Additionally, it will set
*        the user's SCSI status. This is only used for NCQ uncorrectable
*        read errors
*
* @param[in]     sequence This parameter specifies the translation sequence
*                for which to construct the sense data.
* @param[in,out] scsi_io This parameter specifies the user's IO request
*                for which to construct the sense data.
* @param[in]     ata_input_data This parameter specifies the user's ATA IO
*                response from a Read Log Ext command.
* @param[in]     scsi_status This parameter specifies the SCSI status
*                value for the user's IO request.
* @param[in]     sense_key This parameter specifies the sense key to
*                be set for the user's IO request.
* @param[in]     additional_sense_code This parameter specifies the
*                additional sense code (ASC) key to be set for the user's
*                IO request.
* @param[in]     additional_sense_code_qualifier This parameter specifies
*                the additional sense code qualifier (ASCQ) key to be set
*                for the user's IO request.
*
* @return none
*/
static
void sati_scsi_read_ncq_error_descriptor_sense_construct(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_input_data,
   U8                           scsi_status,
   U8                           response_code,
   U8                           sense_key,
   U8                           additional_sense_code,
   U8                           additional_sense_code_qualifier
)
{
   U8 * sense_data;
   U32  sense_len;

   U8 information_buff[8] = {0};

   ATA_NCQ_COMMAND_ERROR_LOG_T * ncq_log = (ATA_NCQ_COMMAND_ERROR_LOG_T *) ata_input_data;

   sati_scsi_get_sense_data_buffer(sequence, scsi_io, scsi_status, &sense_data, &sense_len);

   sati_set_sense_data_byte(
      sense_data,
      sense_len,
      0,
      response_code
   );

   information_buff[2] = ncq_log->lba_47_40;
   information_buff[3] = ncq_log->lba_39_32;
   information_buff[4] = ncq_log->lba_31_24;
   information_buff[5] = ncq_log->lba_23_16;
   information_buff[6] = ncq_log->lba_15_8;
   information_buff[7] = ncq_log->lba_7_0;

   sati_scsi_common_descriptor_sense_construct(
           sequence,
           scsi_io,
           sense_data,
           sense_len,
           sense_key,
           additional_sense_code,
           additional_sense_code_qualifier,
           information_buff
   );
}

/**
* @brief This method will construct the sense data buffer in the user's
*        sense data buffer location.  Additionally, it will set the user's
*        SCSI status. This is only used for NCQ uncorrectable read errors
*
* @param[in]     sequence This parameter specifies the translation sequence
*                for which to construct the sense data.
* @param[in,out] scsi_io This parameter specifies the user's IO request
*                for which to construct the sense data.
* @param[in]     ata_input_data This parameter specifies the user's ATA IO
*                response from a Read Log Ext command.
* @param[in]     scsi_status This parameter specifies the SCSI status
*                value for the user's IO request.
* @param[in]     sense_key This parameter specifies the sense key to
*                be set for the user's IO request.
* @param[in]     additional_sense_code This parameter specifies the
*                additional sense code (ASC) key to be set for the user's
*                IO request.
* @param[in]     additional_sense_code_qualifier This parameter specifies
*                the additional sense code qualifier (ASCQ) key to be set
*                for the user's IO request.
*
* @return none
*/
static
void sati_scsi_read_ncq_error_fixed_sense_construct(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_input_data,
   U8                           scsi_status,
   U8                           response_code,
   U8                           sense_key,
   U8                           additional_sense_code,
   U8                           additional_sense_code_qualifier
)
{
   U8 * sense_data;
   U32  sense_len;
   U8   valid = TRUE;

   ATA_NCQ_COMMAND_ERROR_LOG_T * ncq_log = (ATA_NCQ_COMMAND_ERROR_LOG_T *) ata_input_data;

   sati_scsi_get_sense_data_buffer(sequence, scsi_io, scsi_status, &sense_data, &sense_len);

   if(ncq_log->lba_39_32 > 0)
   {
      valid = FALSE;
   }

   sati_set_sense_data_byte(
      sense_data,
      sense_len,
      0,
      (valid << 7) | response_code
   );

   sati_set_sense_data_byte(sense_data, sense_len, 3,  ncq_log->lba_31_24);
   sati_set_sense_data_byte(sense_data, sense_len, 4,  ncq_log->lba_23_16);
   sati_set_sense_data_byte(sense_data, sense_len, 5,  ncq_log->lba_15_8);
   sati_set_sense_data_byte(sense_data, sense_len, 6,  ncq_log->lba_7_0);

   sati_scsi_common_fixed_sense_construct(
      sense_data,
      sense_len,
      sense_key,
      additional_sense_code,
      additional_sense_code_qualifier
   );
}

void sati_scsi_read_ncq_error_sense_construct(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_input_data,
   U8                           scsi_status,
   U8                           sense_key,
   U8                           additional_sense_code,
   U8                           additional_sense_code_qualifier
)
{
    U8 response_code;

    response_code = sati_scsi_get_sense_data_response_code(sequence);

    switch (response_code)
    {
    case SCSI_FIXED_CURRENT_RESPONSE_CODE:
    case SCSI_FIXED_DEFERRED_RESPONSE_CODE:
        sati_scsi_read_ncq_error_fixed_sense_construct(sequence, scsi_io, ata_input_data, scsi_status,
                response_code, sense_key, additional_sense_code, additional_sense_code_qualifier);
    break;
    case SCSI_DESCRIPTOR_CURRENT_RESPONSE_CODE:
    case SCSI_DESCRIPTOR_DEFERRED_RESPONSE_CODE:
        sati_scsi_read_ncq_error_descriptor_sense_construct(sequence, scsi_io, ata_input_data, scsi_status,
                response_code, sense_key, additional_sense_code, additional_sense_code_qualifier);
        break;
    }

    sequence->is_sense_response_set = TRUE;
}

/**
* @brief This method will construct the sense data buffer in the user's
*        sense data buffer location.  Additionally, it will set the user's
*        SCSI status. This is used for uncorrectable read errors.
*
* @param[in]     sequence This parameter specifies the translation sequence
*                for which to construct the sense data.
* @param[in,out] scsi_io This parameter specifies the user's IO request
*                for which to construct the sense data.
* @param[in]     ata_io This parameter is a pointer to the ATA IO data used
*                to get the ATA register fis.
* @param[in]     scsi_status This parameter specifies the SCSI status
*                value for the user's IO request.
* @param[in]     sense_key This parameter specifies the sense key to
*                be set for the user's IO request.
* @param[in]     additional_sense_code This parameter specifies the
*                additional sense code (ASC) key to be set for the user's
*                IO request.
* @param[in]     additional_sense_code_qualifier This parameter specifies
*                the additional sense code qualifier (ASCQ) key to be set
*                for the user's IO request.
*
* @return none
*/
static
void sati_scsi_read_error_descriptor_sense_construct(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io,
   U8                           scsi_status,
   U8                           response_code,
   U8                           sense_key,
   U8                           additional_sense_code,
   U8                           additional_sense_code_qualifier
)
{
   U8 * sense_data;
   U32  sense_len;
   U8 information_buff[8] = {0};

   U8 * register_fis = sati_cb_get_d2h_register_fis_address(ata_io);

   sati_scsi_get_sense_data_buffer(sequence, scsi_io, scsi_status, &sense_data, &sense_len);

   information_buff[2] = sati_get_ata_lba_high_ext(register_fis);
   information_buff[3] = sati_get_ata_lba_mid_ext(register_fis);
   information_buff[4] = sati_get_ata_lba_low_ext(register_fis);
   information_buff[5] = sati_get_ata_lba_high(register_fis);
   information_buff[6] = sati_get_ata_lba_mid(register_fis);
   information_buff[7] = sati_get_ata_lba_low(register_fis);

   sati_set_sense_data_byte(
      sense_data,
      sense_len,
      0,
      SCSI_DESCRIPTOR_CURRENT_RESPONSE_CODE
   );

   sati_scsi_common_descriptor_sense_construct(
      sequence,
      scsi_io,
      sense_data,
      sense_len,
      sense_key,
      additional_sense_code,
      additional_sense_code_qualifier,
      information_buff
   );
}

/**
* @brief This method will construct the sense data buffer in the user's
*        sense data buffer location.  Additionally, it will set the user's
*        SCSI status. This is used for uncorrectable read errors.
*
* @param[in]     sequence This parameter specifies the translation sequence
*                for which to construct the sense data.
* @param[in,out] scsi_io This parameter specifies the user's IO request
*                for which to construct the sense data.
* @param[in]     ata_io This parameter is a pointer to the ATA IO data used
*                to get the ATA register fis.
* @param[in]     scsi_status This parameter specifies the SCSI status
*                value for the user's IO request.
* @param[in]     sense_key This parameter specifies the sense key to
*                be set for the user's IO request.
* @param[in]     additional_sense_code This parameter specifies the
*                additional sense code (ASC) key to be set for the user's
*                IO request.
* @param[in]     additional_sense_code_qualifier This parameter specifies
*                the additional sense code qualifier (ASCQ) key to be set
*                for the user's IO request.
*
* @return none
*/
static
void sati_scsi_read_error_fixed_sense_construct(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io,
   U8                           scsi_status,
   U8                           response_code,
   U8                           sense_key,
   U8                           additional_sense_code,
   U8                           additional_sense_code_qualifier
)
{
   U8 * sense_data;
   U32  sense_len;
   U8   valid = TRUE;

   U8 * register_fis = sati_cb_get_d2h_register_fis_address(ata_io);

   sati_scsi_get_sense_data_buffer(sequence, scsi_io, scsi_status, &sense_data, &sense_len);

   if(sati_get_ata_lba_mid_ext(register_fis) > 0)
   {
      valid = FALSE;
   }

   sati_set_sense_data_byte(sense_data, sense_len, 3,  sati_get_ata_lba_low_ext(register_fis));
   sati_set_sense_data_byte(sense_data, sense_len, 4,  sati_get_ata_lba_high(register_fis));
   sati_set_sense_data_byte(sense_data, sense_len, 5,  sati_get_ata_lba_mid(register_fis));
   sati_set_sense_data_byte(sense_data, sense_len, 6,  sati_get_ata_lba_low(register_fis));


   sati_set_sense_data_byte(
      sense_data,
      sense_len,
      0,
      (valid << 7) | SCSI_FIXED_CURRENT_RESPONSE_CODE
   );

   sati_scsi_common_fixed_sense_construct(
      sense_data,
      sense_len,
      sense_key,
      additional_sense_code,
      additional_sense_code_qualifier
   );
}

void sati_scsi_read_error_sense_construct(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_input_data,
   U8                           scsi_status,
   U8                           sense_key,
   U8                           additional_sense_code,
   U8                           additional_sense_code_qualifier
)
{
    U8 response_code;

    response_code = sati_scsi_get_sense_data_response_code(sequence);

    switch (response_code)
    {
    case SCSI_FIXED_CURRENT_RESPONSE_CODE:
    case SCSI_FIXED_DEFERRED_RESPONSE_CODE:
        sati_scsi_read_error_fixed_sense_construct(sequence, scsi_io, ata_input_data, scsi_status,
                response_code, sense_key, additional_sense_code, additional_sense_code_qualifier);
    break;
    case SCSI_DESCRIPTOR_CURRENT_RESPONSE_CODE:
    case SCSI_DESCRIPTOR_DEFERRED_RESPONSE_CODE:
        sati_scsi_read_error_descriptor_sense_construct(sequence, scsi_io, ata_input_data, scsi_status,
                response_code, sense_key, additional_sense_code, additional_sense_code_qualifier);
        break;
    }

    sequence->is_sense_response_set = TRUE;
}

/*
 * @brief This method builds the scsi response data for a sata task management
 *        request.
 *
 * @param[in]     sequence This parameter specifies the translation sequence
 *                for which to construct the sense data.
 * @param[in,out] scsi_io This parameter specifies the user's IO request
 *                for which to construct the sense data.
 * @param[in]     response_data The response status for the task management
 *                request.
 */
void sati_scsi_response_data_construct(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   U8                           response_data
)
{
#ifdef SATI_TRANSPORT_SUPPORTS_SAS
   SCI_SSP_RESPONSE_IU_T * rsp_iu  = (SCI_SSP_RESPONSE_IU_T*)
                                        sati_cb_get_response_iu_address(scsi_io);
   rsp_iu->data_present            = 0x01;
   rsp_iu->response_data_length[3] = sizeof(U32);
   rsp_iu->status                  = 0;
   ((U8 *)rsp_iu->data)[3]         = response_data;
#else
#endif // SATI_TRANSPORT_SUPPORTS_SAS
}

/**
 * @brief This method checks to make sure that the translation isn't
 *        exceeding the allocation length specified in the CDB prior
 *        to retrieving the payload data byte from the user's buffer.
 *
 * @param[in,out] scsi_io This parameter specifies the user's IO request
 *                for which to set the user payload data byte.
 * @param[in]     byte_offset This parameter specifies the offset into
 *                the user's payload buffer at which to write the supplied
 *                value.
 * @param[in]     value This parameter specifies the memory location into
 *                which to read the value from the user's payload buffer.
 *
 * @return none
 */
void sati_get_data_byte(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   U32                          byte_offset,
   U8                         * value
)
{
   if (byte_offset < sequence->allocation_length)
      sati_cb_get_data_byte(scsi_io, byte_offset, value);
}

/**
 * @brief This method checks to make sure that the translation isn't
 *        exceeding the allocation length specified in the CDB while
 *        translating payload data into the user's buffer.
 *
 * @param[in]     sequence This parameter specifies the translation sequence
 *                for which to set the user payload data byte.
 * @param[in,out] scsi_io This parameter specifies the user's IO request
 *                for which to set the user payload data byte.
 * @param[in]     byte_offset This parameter specifies the offset into
 *                the user's payload buffer at which to write the supplied
 *                value.
 * @param[in]     value This parameter specifies the new value to be
 *                written out into the user's payload buffer.
 *
 * @return none
 */
void sati_set_data_byte(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   U32                          byte_offset,
   U8                           value
)
{
   if (byte_offset < sequence->allocation_length)
   {
      sequence->number_data_bytes_set++;
      sati_cb_set_data_byte(scsi_io, byte_offset, value);
   }
}

/**
 * @brief This method checks to make sure that the translation isn't
 *        exceeding the allocation length specified in the CDB while
 *        translating payload data into the user's buffer.
 *
 * @param[in]     sequence This parameter specifies the translation sequence
 *                for which to set the user payload data dword.
 * @param[in,out] scsi_io This parameter specifies the user's IO request
 *                for which to set the user payload data dword.
 * @param[in]     byte_offset This parameter specifies the offset into
 *                the user's payload buffer at which to write the supplied
 *                value.
 * @param[in]     value This parameter specifies the new value to be
 *                written out into the user's payload buffer.
 *
 * @return none
 */
void sati_set_data_dword(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   U32                          byte_offset,
   U32                          value
)
{
   /// @todo Check to ensure that the bytes appear correctly (SAS Address).

   sati_set_data_byte(sequence, scsi_io, byte_offset, (U8)value & 0xFF);
       byte_offset++;
   sati_set_data_byte(sequence, scsi_io, byte_offset, (U8)(value >> 8) & 0xFF);
       byte_offset++;
   sati_set_data_byte(sequence, scsi_io, byte_offset, (U8)(value >> 16) & 0xFF);
       byte_offset++;
   sati_set_data_byte(sequence, scsi_io, byte_offset, (U8)(value >> 24) & 0xFF);
}

/**
 * @brief This method will construct the ATA flush cache command.
 *
 * @pre It is expected that the user has properly set the current contents
 *      of the register FIS to 0.
 *
 * @param[out] ata_io This parameter specifies the ATA IO request structure
 *             for which to build the FLUSH CACHE command.
 * @param[in]  sequence This parameter specifies the translator sequence
 *             for which the command is being constructed.
 *
 * @return none.
 */
void sati_ata_flush_cache_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence
)
{
   U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);

   sati_set_ata_command(register_fis, ATA_FLUSH_CACHE);
   sati_ata_non_data_command(ata_io, sequence);
}

/**
 * @brief This method will construct the ATA standby immediate command.
 *
 * @pre It is expected that the user has properly set the current contents
 *      of the register FIS to 0.
 *
 * @param[out] ata_io This parameter specifies the ATA IO request structure
 *             for which to build the STANDBY IMMEDIATE command.
 * @param[in]  sequence This parameter specifies the translator sequence
 *             for which the command is being constructed.
 *
 * @param[in]  count This parameter specifies the time period programmed
 *             into the Standby Timer. See ATA8 spec for more details
 * @return none.
 */
void sati_ata_standby_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   U16                          count
)
{
   U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);

   sati_set_ata_command(register_fis, ATA_STANDBY);
   sati_set_ata_sector_count(register_fis, count);

   sequence->device->ata_standby_timer = (U8) count;

   sati_ata_non_data_command(ata_io, sequence);
}

/**
 * @brief This method will construct the ATA standby immediate command.
 *
 * @pre It is expected that the user has properly set the current contents
 *      of the register FIS to 0.
 *
 * @param[out] ata_io This parameter specifies the ATA IO request structure
 *             for which to build the STANDBY IMMEDIATE command.
 * @param[in]  sequence This parameter specifies the translator sequence
 *             for which the command is being constructed.
 *
 * @return none.
 */
void sati_ata_standby_immediate_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence
)
{
   U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);

   sati_set_ata_command(register_fis, ATA_STANDBY_IMMED);
   sati_ata_non_data_command(ata_io, sequence);
}

/**
 * @brief This method will construct the ATA idle immediate command.
 *
 * @pre It is expected that the user has properly set the current contents
 *      of the register FIS to 0.
 *
 * @param[out] ata_io This parameter specifies the ATA IO request structure
 *             for which to build the IDLE IMMEDIATE command.
 * @param[in]  sequence This parameter specifies the translator sequence
 *             for which the command is being constructed.
 *
 * @return none.
 */
void sati_ata_idle_immediate_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence
)
{
   U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);

   sati_set_ata_command(register_fis, ATA_IDLE_IMMED);
   sati_set_ata_features(register_fis, 0x00);
   sati_set_ata_sector_count(register_fis, 0x00);
   sati_set_ata_lba_high(register_fis, 0x00);
   sati_set_ata_lba_mid(register_fis, 0x00);
   sati_set_ata_lba_low(register_fis, 0x00);
   sati_ata_non_data_command(ata_io, sequence);
}

/**
 * @brief This method will construct the ATA idle immediate command
          for Unload Features.
 *
 * @pre It is expected that the user has properly set the current contents
 *      of the register FIS to 0.
 *
 * @param[out] ata_io This parameter specifies the ATA IO request structure
 *             for which to build the IDLE IMMEDIATE command.
 * @param[in]  sequence This parameter specifies the translator sequence
 *             for which the command is being constructed.
 *
 * @return none.
 */
void sati_ata_idle_immediate_unload_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence
)
{
   U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);

   sati_set_ata_command(register_fis, ATA_IDLE_IMMED);
   sati_set_ata_features(register_fis, 0x44);
   sati_set_ata_sector_count(register_fis, 0x00);
   sati_set_ata_lba_high(register_fis, 0x55);
   sati_set_ata_lba_mid(register_fis, 0x4E);
   sati_set_ata_lba_low(register_fis, 0x4C);
   sati_ata_non_data_command(ata_io, sequence);
}

/**
 * @brief This method will construct the ATA IDLE command.\
 *
 * @pre It is expected that the user has properly set the current contents
 *      of the register FIS to 0.
 *
 * @param[out] ata_io This parameter specifies the ATA IO request structure
 *             for which to build the ATA IDLE command.
 * @param[in]  sequence This parameter specifies the translator sequence
 *             for which the command is being constructed.
 *
 * @return none.
 */
void sati_ata_idle_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence
)
{
   U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);

   sati_set_ata_command(register_fis, ATA_IDLE);
   sati_set_ata_features(register_fis, 0x00);
   sati_set_ata_sector_count(register_fis, 0x00);

   sequence->device->ata_standby_timer = 0x00;

   sati_set_ata_lba_high(register_fis, 0x00);
   sati_set_ata_lba_mid(register_fis, 0x00);
   sati_set_ata_lba_low(register_fis, 0x00);
   sati_ata_non_data_command(ata_io, sequence);
}

/**
 * @brief This method will construct the ATA MEDIA EJECT command.
 *
 * @pre It is expected that the user has properly set the current contents
 *      of the register FIS to 0.
 *
 * @param[out] ata_io This parameter specifies the ATA IO request structure
 *             for which to build the MEDIA EJCT command.
 * @param[in]  sequence This parameter specifies the translator sequence
 *             for which the command is being constructed.
 *
 * @return none.
 */
void sati_ata_media_eject_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence
)
{
   U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);

   sati_set_ata_command(register_fis, ATA_MEDIA_EJECT);
   sati_ata_non_data_command(ata_io, sequence);
}


/**
 * @brief This method will construct the ATA read verify sector(s) command.
 *
 * @pre It is expected that the user has properly set the current contents
 *      of the register FIS to 0.
 *
 * @param[out] ata_io This parameter specifies the ATA IO request structure
 *             for which to build the ATA READ VERIFY SECTOR(S) command.
 * @param[in]  sequence This parameter specifies the translator sequence
 *             for which the command is being constructed.
 *
 * @return none.
 */
void sati_ata_read_verify_sectors_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence
)
{
   U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);

   sati_set_ata_command(register_fis, ATA_READ_VERIFY_SECTORS);

   //According to SAT-2 (v7) 9.11.3
   sati_set_ata_sector_count(register_fis, 1);

   //According to SAT-2 (v7) 9.11.3, set LBA to a value between zero and the
   //maximum LBA supported by the ATA device in its current configuration.
   //From the unit test, it seems we have to set LBA to a non-zero value.
   sati_set_ata_lba_low(register_fis, 1);

   sati_ata_non_data_command(ata_io, sequence);
}

/**
 * @brief This method will construct a ATA SMART Return Status command so the
 *        status of the ATA device can be returned. The status of the SMART
 *        threshold will be returned by this command.
 *
 * @return N/A
 *
 */
void sati_ata_smart_return_status_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   U8                           feature_value
)
{
   U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);

   sati_set_ata_command(register_fis, ATA_SMART);

   sati_set_ata_features(register_fis, feature_value);

   sati_set_ata_lba_high(register_fis, 0xC2);
   sati_set_ata_lba_mid(register_fis, 0x4F);

   sati_ata_non_data_command(ata_io, sequence);
}

/**
 * @brief This method will construct a ATA SMART Return Status command so the
 *        status of the ATA device can be returned. The status of the SMART
 *        threshold will be returned by this command.
 *
 * @return N/A
 *
 */
void sati_ata_smart_read_log_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   U8                           log_address,
   U32                          transfer_length
)
{
   U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);

   sati_set_ata_command(register_fis, ATA_SMART);
   sati_set_ata_features(register_fis, ATA_SMART_SUB_CMD_READ_LOG);

   sati_set_ata_lba_high(register_fis, 0xC2);
   sati_set_ata_lba_mid(register_fis, 0x4F);
   sati_set_ata_lba_low(register_fis, log_address);

   sequence->data_direction      = SATI_DATA_DIRECTION_IN;
   sequence->protocol            = SAT_PROTOCOL_PIO_DATA_IN;
   sequence->ata_transfer_length = transfer_length;
}

/**
 * @brief This method will construct a Write Uncorrectable ATA command that
 *        will write one sector with a pseudo or flagged error. The type of
 *        error is specified by the feature value.
 *
 * @return N/A
 *
 */
void sati_ata_write_uncorrectable_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   U8                           feature_value
)
{
   U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);

   sati_set_ata_command(register_fis, ATA_WRITE_UNCORRECTABLE);
   sati_set_ata_features(register_fis, feature_value);
   sati_set_ata_sector_count(register_fis, 0x0001);
   sati_ata_non_data_command(ata_io, sequence);
}

/**
 * @brief This method will construct a Mode Select ATA SET FEATURES command
 *        For example, Enable/Disable Write Cache, Enable/Disable Read Ahead
 *
 * @return N/A
 *
 */
void sati_ata_set_features_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   U8                           feature
)
{
   U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);

   sati_set_ata_command(register_fis, ATA_SET_FEATURES);
   sati_set_ata_features(register_fis, feature);
   sati_ata_non_data_command(ata_io, sequence);
}



/**
 * @brief This method will construct a Read Log ext ATA command that
 *        will request a log page based on the log_address.
 *
 * @param[in]  log_address This parameter specifies the log page
 *             to be returned from Read Log Ext.
 *
 * @param[in]  transfer_length This parameter specifies the size of the
 *             log page response returned by Read Log Ext.
 *
 * @return N/A
 *
 */
void sati_ata_read_log_ext_construct(
   void                          * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T    * sequence,
   U8                              log_address,
   U32                             transfer_length
)
{
   U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);

   sati_set_ata_command(register_fis, ATA_READ_LOG_EXT);

   sati_set_ata_lba_low(register_fis, log_address);
   sati_set_ata_lba_mid(register_fis, 0x00);
   sati_set_ata_lba_mid_exp(register_fis, 0x00);

   sati_set_ata_sector_count(register_fis, 0x01);

   sequence->data_direction      = SATI_DATA_DIRECTION_IN;
   sequence->protocol            = SAT_PROTOCOL_PIO_DATA_IN;
   sequence->ata_transfer_length = transfer_length;

}

/**
* @brief This method will check if the ATA device is in the stopped power
*        state. This is used for all medium access commands for SAT
*        compliance. See SAT2r07 section 9.11.1
*
* @param[in] sequence - SATI sequence data with the device state.
*
* @return TRUE If device is stopped
*
*/
BOOL sati_device_state_stopped(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io
)
{
   if(sequence->device->state == SATI_DEVICE_STATE_STOPPED)
   {
      sati_scsi_sense_data_construct(
         sequence,
         scsi_io,
         SCSI_STATUS_CHECK_CONDITION,
         SCSI_SENSE_NOT_READY ,
         SCSI_ASC_INITIALIZING_COMMAND_REQUIRED,
         SCSI_ASCQ_INITIALIZING_COMMAND_REQUIRED
      );
      return TRUE;
   }
   return FALSE;
}

/**
* @brief This method will construct a ATA Read Buffer command that
*        will request PIO in data containing the target device's buffer.
*
* @param[out] ata_io This parameter specifies the ATA IO request structure
*             for which to build the ATA READ VERIFY SECTOR(S) command.
* @param[in]  sequence This parameter specifies the translator sequence
*             for which the command is being constructed.
* @return N/A
*
*/
void sati_ata_read_buffer_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence
)
{
   U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);

   sati_set_ata_command(register_fis, ATA_READ_BUFFER);
   sequence->data_direction      = SATI_DATA_DIRECTION_IN;
   sequence->protocol            = SAT_PROTOCOL_PIO_DATA_IN;
   sequence->ata_transfer_length = 512;
}


/**
* @brief This method will construct a ATA Write Buffer command that
*        will send PIO out data to the target device's buffer.
*
* @param[out] ata_io This parameter specifies the ATA IO request structure
*             for which to build the ATA READ VERIFY SECTOR(S) command.
* @param[in]  sequence This parameter specifies the translator sequence
*             for which the command is being constructed.
* @return N/A
*
*/
void sati_ata_write_buffer_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence
)
{
   U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);

   sati_set_ata_command(register_fis, ATA_WRITE_BUFFER);

   sequence->data_direction      = SATI_DATA_DIRECTION_OUT;
   sequence->protocol            = SAT_PROTOCOL_PIO_DATA_OUT;
   sequence->ata_transfer_length = 512;
}


/**
* @brief This method will construct a ATA Download Microcode command that
*        will send PIO out data containing new firmware for the target drive.
*
* @param[out] ata_io This parameter specifies the ATA IO request structure
*             for which to build the ATA READ VERIFY SECTOR(S) command.
* @param[in]  sequence This parameter specifies the translator sequence
*             for which the command is being constructed.
* @param[in]  mode This parameter specifies the download microcode sub-command
*             code.
* @param[in]  allocation_length This parameter specifies the number of bytes
*             being sent to the target device.
* @param[in]  buffer_offset This parameter specifies the buffer offset for the
*             data sent to the target device.
*
* @return N/A
*
*/
void sati_ata_download_microcode_construct(
   void                       * ata_io,
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   U8                           mode,
   U32                          allocation_length,
   U32                          buffer_offset
)
{
   U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);
   U32 allocation_blocks = allocation_length >> 9;
   U32 buffer_blkoffset = buffer_offset >> 9;

   sati_set_ata_command(register_fis, ATA_DOWNLOAD_MICROCODE);
   sati_set_ata_features(register_fis, mode);

   if(mode == ATA_MICROCODE_DOWNLOAD_SAVE)
   {
      sati_set_ata_sector_count(register_fis, (U8) (allocation_length >> 9));
      sati_set_ata_lba_low(register_fis, (U8) (allocation_length >> 17));
   }
   else //mode == 0x03
   {
      sati_set_ata_sector_count(register_fis, (U8) (allocation_blocks & 0xff));
      sati_set_ata_lba_low(register_fis, (U8) ((allocation_blocks >> 8) & 0xff));
      sati_set_ata_lba_mid(register_fis, (U8) (buffer_blkoffset & 0xff));
      sati_set_ata_lba_high(register_fis, (U8) ((buffer_blkoffset >> 8) & 0xff));
   }

   if((allocation_length == 0) && (buffer_offset == 0))
   {
      sati_ata_non_data_command(ata_io, sequence);
   }
   else
   {
      sequence->data_direction      = SATI_DATA_DIRECTION_OUT;
      sequence->protocol            = SAT_PROTOCOL_PIO_DATA_OUT;
      sequence->ata_transfer_length = allocation_length;
   }
}
