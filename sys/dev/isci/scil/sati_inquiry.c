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
 *
 * @brief This file contains the method implementations required to
 *        translate the SCSI inquiry command.
 *        The following (VPD) pages are currently supported:
 *        - Standard
 *        - Supported Pages
 *        - Unit Serial Number
 *        - Device Identification
 */

#if !defined(DISABLE_SATI_INQUIRY)

#include <dev/isci/scil/sati_inquiry.h>
#include <dev/isci/scil/sati_callbacks.h>
#include <dev/isci/scil/sati_util.h>
#include <dev/isci/scil/intel_ata.h>
#include <dev/isci/scil/intel_scsi.h>

//******************************************************************************
//* P R I V A T E   M E T H O D S
//******************************************************************************
/**
* @brief This method builds the SCSI data associated with the SATI product
*        revision that is commonly used on the Standard inquiry response and
*        the ATA information page.
*
* @param[in]  sequence This parameter specifies the translator sequence
*             object to be utilized during data translation.
* @param[in]  ata_input_data This parameter specifies ata data received from
*             the remote device.
* @param[out] scsi_io This parameter specifies the user IO request for
*             which to construct the standard inquiry data.
*
* @return none
*/
static
void sati_inquiry_construct_product_revision(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * ata_input_data,
   void                       * scsi_io
)
{
   ATA_IDENTIFY_DEVICE_DATA_T * identify = (ATA_IDENTIFY_DEVICE_DATA_T*)
      ata_input_data;

   // Fill in the product revision level field.
   // Per SAT, copy portions of the firmware revision that is not filled
   // with spaces.  Some devices left-align their firmware rev ID, while
   // others right-align.
   if (  (identify->firmware_revision[4] == 0x20)
       && (identify->firmware_revision[5] == 0x20)
       && (identify->firmware_revision[6] == 0x20)
       && (identify->firmware_revision[7] == 0x20) )
   {
      sati_ata_identify_device_copy_data(
         sequence,
         scsi_io,
         32,
         ata_input_data,
         ATA_IDENTIFY_DEVICE_GET_OFFSET(firmware_revision),
         4,
         TRUE
       );
   }
   else
   {
      // Since the last 4 bytes of the firmware revision are not spaces,
      // utilize these bytes as the firmware revision in the inquiry data.
      sati_ata_identify_device_copy_data(
         sequence,
         scsi_io,
         32,
         ata_input_data,
         ATA_IDENTIFY_DEVICE_GET_OFFSET(firmware_revision)+4,
         4,
         TRUE
      );
   }
}


//******************************************************************************
//* P U B L I C   M E T H O D S
//******************************************************************************

/**
 * @brief This method builds the SCSI data associated with a SCSI standard
 *        inquiry request.
 *
 * @param[in]  sequence This parameter specifies the translator sequence
 *             object to be utilized during data translation.
 * @param[in]  ata_input_data This parameter specifies ata data received from
 *             the remote device.
 * @param[out] scsi_io This parameter specifies the user IO request for
 *             which to construct the standard inquiry data.
 *
 * @return none
 */
void sati_inquiry_standard_translate_data(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * ata_input_data,
   void                       * scsi_io
)
{
   ATA_IDENTIFY_DEVICE_DATA_T * identify = (ATA_IDENTIFY_DEVICE_DATA_T*)
                                           ata_input_data;
   U32  index;

   // Device type is disk, attached to this lun.
   sati_set_data_byte(sequence, scsi_io, 0, 0x00);

   // If the device indicates it's a removable media device, then set the
   // RMB bit
   if (identify->general_config_bits & ATA_IDENTIFY_REMOVABLE_MEDIA_ENABLE)
      sati_set_data_byte(sequence, scsi_io, 1, 0x80);
   else
      sati_set_data_byte(sequence, scsi_io, 1, 0x00);

   sati_set_data_byte(sequence, scsi_io, 2, 0x05); // Indicate SPC-3 support
   sati_set_data_byte(sequence, scsi_io, 3, 0x02); // Response Format SPC-3

   sati_set_data_byte(sequence, scsi_io, 4, 62); // 62 Additional Data Bytes.
                                                 // n-4 per the spec, we end at
                                                 // byte 66, so 66-4.
   sati_set_data_byte(sequence, scsi_io, 5, 0x00);
   sati_set_data_byte(sequence, scsi_io, 6, 0x00);
   sati_set_data_byte(sequence, scsi_io, 7, 0x02); // Enable Cmd Queueing

   // The Vender identification field is set to "ATA     "
   sati_set_data_byte(sequence, scsi_io, 8, 0x41);
   sati_set_data_byte(sequence, scsi_io, 9, 0x54);
   sati_set_data_byte(sequence, scsi_io, 10, 0x41);
   sati_set_data_byte(sequence, scsi_io, 11, 0x20);
   sati_set_data_byte(sequence, scsi_io, 12, 0x20);
   sati_set_data_byte(sequence, scsi_io, 13, 0x20);
   sati_set_data_byte(sequence, scsi_io, 14, 0x20);
   sati_set_data_byte(sequence, scsi_io, 15, 0x20);

   // Fill in the product ID field.
   sati_ata_identify_device_copy_data(
      sequence,
      scsi_io,
      16,
      ata_input_data,
      ATA_IDENTIFY_DEVICE_GET_OFFSET(model_number),
      16,
      TRUE
   );

   sati_inquiry_construct_product_revision(
      sequence,
      ata_input_data,
      scsi_io
   );

   // Set the remaining fields up to the version descriptors to 0.
   for (index = 36; index < 58; index++)
      sati_set_data_byte(sequence, scsi_io, index, 0);

   // Add version descriptors for the various protocols in play.

   // SAM-4
   sati_set_data_byte(sequence, scsi_io, 58, 0);
   sati_set_data_byte(sequence, scsi_io, 59, 0x80);

   // SAS-2
   sati_set_data_byte(sequence, scsi_io, 60, 0x0C);
   sati_set_data_byte(sequence, scsi_io, 61, 0x20);

   // SPC-4
   sati_set_data_byte(sequence, scsi_io, 62, 0x04);
   sati_set_data_byte(sequence, scsi_io, 63, 0x60);

   // SBC-3
   sati_set_data_byte(sequence, scsi_io, 64, 0x04);
   sati_set_data_byte(sequence, scsi_io, 65, 0xC0);

   // ATA/ATAPI-8 ACS
   sati_set_data_byte(sequence, scsi_io, 66, 0x16);
   sati_set_data_byte(sequence, scsi_io, 67, 0x23);
}

/**
 * @brief This method builds the SCSI data associated with an SCSI inquiry
 *        for the supported VPD pages page.
 *
 * @param[in]  sequence This parameter specifies the translator sequence
 *             object to be utilized during data translation.
 * @param[out] scsi_io This parameter specifies the user IO request for
 *             which to construct the supported VPD page information.
 *
 * @return none
 */
static
void sati_inquiry_supported_pages_translate_data(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io
)
{
   // Formulate the SCSI output data for the caller.
   sati_set_data_byte(sequence, scsi_io, 0, 0); // Qualifier and Device Type
   sati_set_data_byte(sequence, scsi_io, 1, SCSI_INQUIRY_SUPPORTED_PAGES_PAGE);
   sati_set_data_byte(sequence, scsi_io, 2, 0); // Reserved.
   sati_set_data_byte(sequence, scsi_io, 3, 4); // # VPD pages supported
   sati_set_data_byte(sequence, scsi_io, 4, SCSI_INQUIRY_SUPPORTED_PAGES_PAGE);
   sati_set_data_byte(sequence, scsi_io, 5, SCSI_INQUIRY_UNIT_SERIAL_NUM_PAGE);
   sati_set_data_byte(sequence, scsi_io, 6, SCSI_INQUIRY_DEVICE_ID_PAGE);
   sati_set_data_byte(sequence, scsi_io, 7, SCSI_INQUIRY_ATA_INFORMATION_PAGE);
   sati_set_data_byte(sequence, scsi_io, 8, SCSI_INQUIRY_BLOCK_DEVICE_PAGE);
   sati_set_data_byte(sequence, scsi_io, 9, 0); // End of the list
}

/**
 * @brief This method builds the SCSI data associated with a request for
 *        the unit serial number vital product data (VPD) page.
 *
 * @param[in]  sequence This parameter specifies the translator sequence
 *             object to be utilized during data translation.
 * @param[in]  ata_input_data This parameter specifies ata data received from
 *             the remote device.
 * @param[out] scsi_io This parameter specifies the user IO request for
 *             which to construct the unit serial number data.
 *
 * @return none
 */
void sati_inquiry_serial_number_translate_data(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * ata_input_data,
   void                       * scsi_io
)
{
   // Peripheral qualifier (0x0, currently connected)
   // Peripheral device type (0x0 direct-access block device)
   sati_set_data_byte(sequence, scsi_io, 0, 0x00);

   sati_set_data_byte(sequence, scsi_io, 1, SCSI_INQUIRY_UNIT_SERIAL_NUM_PAGE);
   sati_set_data_byte(sequence, scsi_io, 2, 0x00);  // Reserved
   sati_set_data_byte(sequence, scsi_io, 3, ATA_IDENTIFY_SERIAL_NUMBER_LEN);

   sati_ata_identify_device_copy_data(
      sequence,
      scsi_io,
      4,
      ata_input_data,
      ATA_IDENTIFY_DEVICE_GET_OFFSET(serial_number),
      ATA_IDENTIFY_SERIAL_NUMBER_LEN,
      TRUE
   );
}

/**
* @brief This method builds the SCSI data associated with a request for
*        the Block Device Characteristics vital product data (VPD) page.
*
* @param[in]  sequence This parameter specifies the translator sequence
*             object to be utilized during data translation.
* @param[in]  ata_input_data This parameter specifies ata data received from
*             the remote device.
* @param[out] scsi_io This parameter specifies the user IO request for
*             which to construct the unit serial number data.
*
* @return none
*/
void sati_inquiry_block_device_translate_data(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * ata_input_data,
   void                       * scsi_io
)
{
   ATA_IDENTIFY_DEVICE_DATA_T * identify = (ATA_IDENTIFY_DEVICE_DATA_T*)
      ata_input_data;

   U32 offset;

   // Peripheral qualifier (0x0, currently connected)
   // Peripheral device type (0x0 direct-access block device)
   sati_set_data_byte(sequence, scsi_io, 0, 0x00);

   sati_set_data_byte(sequence, scsi_io, 1, SCSI_INQUIRY_BLOCK_DEVICE_PAGE);

   //PAGE LENGTH 0x003C
   sati_set_data_byte(sequence, scsi_io, 2, 0x00);
   sati_set_data_byte(sequence, scsi_io, 3, SCSI_INQUIRY_BLOCK_DEVICE_LENGTH);

   sati_ata_identify_device_copy_data(
      sequence,
      scsi_io,
      4,
      ata_input_data,
      ATA_IDENTIFY_DEVICE_GET_OFFSET(nominal_media_rotation_rate),
      2,
      FALSE
    );

    sati_set_data_byte(sequence, scsi_io, 6, 0x00);

    sati_set_data_byte(
       sequence,
       scsi_io,
       7,
       (identify->device_nominal_form_factor & 0x0F) // only need bits 0-3
    );

    //bytes 8-63 are reserved
    for(offset = 8; offset < 64; offset++)
    {
       sati_set_data_byte(sequence, scsi_io, offset, 0x00);
    }
}

/**
 * @brief This method builds the SCSI data associated with a request for
 *        the device identification vital product data (VPD) page.
 *
 * @param[in]  sequence This parameter specifies the translator sequence
 *             object to be utilized during data translation.
 * @param[in]  ata_input_data This parameter specifies ata data received from
 *             the remote device.
 * @param[out] scsi_io This parameter specifies the user IO request for
 *             which to construct the device ID page.
 *
 * @return none
 */
void sati_inquiry_device_id_translate_data(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * ata_input_data,
   void                       * scsi_io
)
{
   ATA_IDENTIFY_DEVICE_DATA_T * identify    = (ATA_IDENTIFY_DEVICE_DATA_T*)
                                              ata_input_data;
   U16                     byte_offset = 4;
   U16                     page_length;

   // Peripheral qualifier (0x0, currently connected)
   // Peripheral device type (0x0 direct-access block device)
   sati_set_data_byte(sequence, scsi_io, 0, 0x00);

   sati_set_data_byte(sequence, scsi_io, 1, SCSI_INQUIRY_DEVICE_ID_PAGE);

   /**
    * If World Wide Names are supported by this target, then build an
    * identification descriptor using the WWN.
    */

   if (identify->command_set_supported_extention
       & ATA_IDENTIFY_COMMAND_SET_WWN_SUPPORT_ENABLE)
   {

      sati_set_data_byte(sequence,
         scsi_io, 4, SCSI_FC_PROTOCOL_IDENTIFIER | SCSI_BINARY_CODE_SET
      );


      sati_set_data_byte(sequence,
         scsi_io, 5, SCSI_LUN_ASSOCIATION | SCSI_NAA_IDENTIFIER_TYPE
      );

      sati_set_data_byte(sequence, scsi_io, 6, 0);
      sati_set_data_byte(sequence, scsi_io, 7, 0x08); // WWN are 8 bytes long

      // Copy data from the identify device world wide name field into the
      // buffer.
      sati_ata_identify_device_copy_data(
         sequence,
         scsi_io,
         8,
         ata_input_data,
         ATA_IDENTIFY_DEVICE_GET_OFFSET(world_wide_name),
         ATA_IDENTIFY_WWN_LEN,
         FALSE
      );

      byte_offset = 16;
   }

   /**
    * Build a identification descriptor using the model number & serial number.
    */

   sati_set_data_byte(sequence,
      scsi_io, byte_offset, SCSI_FC_PROTOCOL_IDENTIFIER | SCSI_ASCII_CODE_SET
   );
   byte_offset++;
   sati_set_data_byte(sequence,
      scsi_io, byte_offset, SCSI_LUN_ASSOCIATION | SCSI_T10_IDENTIFIER_TYPE
   );
   byte_offset++;
   sati_set_data_byte(sequence, scsi_io, byte_offset, 0);
   byte_offset++;

   // Identifier length (8 bytes for "ATA     " + 40 bytes from ATA IDENTIFY
   // model number field + 20 bytes from ATA IDENTIFY serial number field.
   sati_set_data_byte(
      sequence,
      scsi_io,
      byte_offset,
      8 + (ATA_IDENTIFY_SERIAL_NUMBER_LEN) + (ATA_IDENTIFY_MODEL_NUMBER_LEN)
   );
   byte_offset++;

   // Per SAT, write "ATA     ".
   sati_set_data_byte(sequence, scsi_io, byte_offset, 0x41);
   byte_offset++;
   sati_set_data_byte(sequence, scsi_io, byte_offset, 0x54);
   byte_offset++;
   sati_set_data_byte(sequence, scsi_io, byte_offset, 0x41);
   byte_offset++;
   sati_set_data_byte(sequence, scsi_io, byte_offset, 0x20);
   byte_offset++;
   sati_set_data_byte(sequence, scsi_io, byte_offset, 0x20);
   byte_offset++;
   sati_set_data_byte(sequence, scsi_io, byte_offset, 0x20);
   byte_offset++;
   sati_set_data_byte(sequence, scsi_io, byte_offset, 0x20);
   byte_offset++;
   sati_set_data_byte(sequence, scsi_io, byte_offset, 0x20);
   byte_offset++;

   // Copy data from the identify device model number field into the
   // buffer and update the byte_offset.
   sati_ata_identify_device_copy_data(
      sequence,
      scsi_io,
      byte_offset,
      ata_input_data,
      ATA_IDENTIFY_DEVICE_GET_OFFSET(model_number),
      ATA_IDENTIFY_MODEL_NUMBER_LEN,
      TRUE
   );

   byte_offset += ATA_IDENTIFY_MODEL_NUMBER_LEN;

   // Copy data from the identify device serial number field into the
   // buffer and update the byte_offset.
   sati_ata_identify_device_copy_data(
      sequence,
      scsi_io,
      byte_offset,
      ata_input_data,
      ATA_IDENTIFY_DEVICE_GET_OFFSET(serial_number),
      ATA_IDENTIFY_SERIAL_NUMBER_LEN,
      TRUE
   );

   byte_offset += ATA_IDENTIFY_SERIAL_NUMBER_LEN;

   /**
    * If the target is contained in a SAS Domain, then build a target port
    * ID descriptor using the SAS address.
    */

#if     defined(SATI_TRANSPORT_SUPPORTS_SAS)       \
     && defined(DISABLE_MSFT_SCSI_COMPLIANCE_SUPPORT)
   {
      SCI_SAS_ADDRESS_T sas_address;

      sati_set_data_byte(
         sequence,
         scsi_io,
         byte_offset,
         SCSI_SAS_PROTOCOL_IDENTIFIER | SCSI_BINARY_CODE_SET
      );
      byte_offset++;

      sati_set_data_byte(
         sequence,
         scsi_io,
         byte_offset,
         SCSI_PIV_ENABLE | SCSI_TARGET_PORT_ASSOCIATION |
         SCSI_NAA_IDENTIFIER_TYPE
      );

      byte_offset++;
      sati_set_data_byte(sequence, scsi_io, byte_offset, 0);
      byte_offset++;
      sati_set_data_byte(sequence, scsi_io, byte_offset, 8); // SAS Addr=8 bytes
      byte_offset++;

      sati_cb_device_get_sas_address(scsi_io, &sas_address);

      // Store the SAS address in the target port descriptor.
      sati_set_data_dword(sequence, scsi_io, byte_offset, sas_address.high);
      byte_offset += 4;
      sati_set_data_dword(sequence, scsi_io, byte_offset, sas_address.low);
      byte_offset += 4;
   }
#endif // SATI_TRANSPORT_SUPPORTS_SAS && DISABLE_MSFT_SCSI_COMPLIANCE_SUPPORT

   /**
    * Set the Page length field.  The page length is n-3, where n is the
    * last offset in the page (considered page length - 4).
    */

   page_length = byte_offset - 4;
   sati_set_data_byte(sequence, scsi_io, 2, (U8)((page_length & 0xFF00) >> 8));
   sati_set_data_byte(sequence, scsi_io, 3, (U8)(page_length & 0x00FF));
}

/**
* @brief This method builds the SCSI data associated with a request for
*        the  ATA information vital product data (VPD) page.
*
* @param[in]  sequence This parameter specifies the translator sequence
*             object to be utilized during data translation.
* @param[in]  ata_input_data This parameter specifies ata data received from
*             a identify device command processed by the remote device.
* @param[out] scsi_io This parameter specifies the user IO request for
*             which to construct the ATA information page.
*
* @return none
*/
SATI_STATUS sati_inquiry_ata_information_translate_data(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * ata_input_data,
   void                       * scsi_io
)
{
   sati_set_data_byte(sequence, scsi_io, 0, 0x00);
   sati_set_data_byte(sequence, scsi_io, 1, SCSI_INQUIRY_ATA_INFORMATION_PAGE);
   sati_set_data_byte(sequence, scsi_io, 2, 0x02);
   sati_set_data_byte(sequence, scsi_io, 3, 0x38);

   //Reserved SAT2r07
   sati_set_data_byte(sequence, scsi_io, 4, 0x00);
   sati_set_data_byte(sequence, scsi_io, 5, 0x00);
   sati_set_data_byte(sequence, scsi_io, 6, 0x00);
   sati_set_data_byte(sequence, scsi_io, 7, 0x00);

   // The Vender identification field is set to "ATA     "
   sati_set_data_byte(sequence, scsi_io, 8, 0x41);
   sati_set_data_byte(sequence, scsi_io, 9, 0x54);
   sati_set_data_byte(sequence, scsi_io, 10, 0x41);
   sati_set_data_byte(sequence, scsi_io, 11, 0x20);
   sati_set_data_byte(sequence, scsi_io, 12, 0x20);
   sati_set_data_byte(sequence, scsi_io, 13, 0x20);
   sati_set_data_byte(sequence, scsi_io, 14, 0x20);
   sati_set_data_byte(sequence, scsi_io, 15, 0x20);

   //SAT Product identification
   sati_ata_identify_device_copy_data(
      sequence,
      scsi_io,
      16,
      ata_input_data,
      ATA_IDENTIFY_DEVICE_GET_OFFSET(model_number),
      16,
      TRUE
   );

   //SAT Product Revision level bytes 32-35
   sati_inquiry_construct_product_revision(
      sequence,
      ata_input_data,
      scsi_io
   );

   //skipping ATA device signature for now

   //Command code
   sati_set_data_byte(sequence, scsi_io, 56, 0xEC);

   //Reserved SAT2r07
   sati_set_data_byte(sequence, scsi_io, 57, 0x00);
   sati_set_data_byte(sequence, scsi_io, 58, 0x00);
   sati_set_data_byte(sequence, scsi_io, 59, 0x00);

   //copy all ATA identify device data
   sati_ata_identify_device_copy_data(
      sequence,
      scsi_io,
      60,
      ata_input_data,
      0,
      sizeof(ATA_IDENTIFY_DEVICE_DATA_T),
      FALSE
   );

   //Need to send ATA Execute Device Diagnostic command still
   sequence->state = SATI_SEQUENCE_STATE_INCOMPLETE;

   return SATI_SEQUENCE_INCOMPLETE;
}

/**
 * @brief This method will translate the inquiry SCSI command into
 *        an ATA IDENTIFY DEVICE command.  It will handle several different
 *        VPD pages and the standard inquiry page.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @return Indicate if the command translation succeeded.
 * @retval SCI_SUCCESS This is returned if the command translation was
 *         successful.
 * @retval SATI_FAILURE_CHECK_RESPONSE_DATA This value is returned if
 *         the page isn't supported, or the page code
 *         field is not zero when the EVPD bit is 0.
 */
SATI_STATUS sati_inquiry_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   U8 * cdb = sati_cb_get_cdb_address(scsi_io);

   /**
    * SPC dictates:
    * - that the page code field must be 0, if VPD enable is 0.
    */
   if (  ((sati_get_cdb_byte(cdb, 1) & SCSI_INQUIRY_EVPD_ENABLE) == 0)
      && (sati_get_cdb_byte(cdb, 2) != 0) )
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

   // Set the data length based on the allocation length field in the CDB.
   sequence->allocation_length = (sati_get_cdb_byte(cdb, 3) << 8) |
                                 (sati_get_cdb_byte(cdb, 4));

   // Check to see if there was a request for the vital product data or just
   // the standard inquiry.
   if (sati_get_cdb_byte(cdb, 1) & SCSI_INQUIRY_EVPD_ENABLE)
   {
      // Parse the page code to determine which translator to invoke.
      switch (sati_get_cdb_byte(cdb, 2))
      {
         case SCSI_INQUIRY_SUPPORTED_PAGES_PAGE:
            sequence->type  = SATI_SEQUENCE_INQUIRY_SUPPORTED_PAGES;
            sati_inquiry_supported_pages_translate_data(sequence, scsi_io);
            return SATI_COMPLETE;
         break;

         case SCSI_INQUIRY_UNIT_SERIAL_NUM_PAGE:
            sequence->type = SATI_SEQUENCE_INQUIRY_SERIAL_NUMBER;
         break;

         case SCSI_INQUIRY_DEVICE_ID_PAGE:
            sequence->type = SATI_SEQUENCE_INQUIRY_DEVICE_ID;
         break;

         case SCSI_INQUIRY_ATA_INFORMATION_PAGE:

            if(sequence->state == SATI_SEQUENCE_STATE_INCOMPLETE)
            {
               sati_ata_execute_device_diagnostic_construct(
                  ata_io,
                  sequence
               );
               sequence->type = SATI_SEQUENCE_INQUIRY_EXECUTE_DEVICE_DIAG;
            }
            else
            {
               sequence->type = SATI_SEQUENCE_INQUIRY_ATA_INFORMATION;
            }
         break;

         case SCSI_INQUIRY_BLOCK_DEVICE_PAGE:
            sequence->type = SATI_SEQUENCE_INQUIRY_BLOCK_DEVICE;
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
            return SATI_FAILURE_CHECK_RESPONSE_DATA;
         break;
      }
   }
   else
   {
      sequence->type = SATI_SEQUENCE_INQUIRY_STANDARD;
   }

   sati_ata_identify_device_construct(ata_io, sequence);

   return SATI_SUCCESS;
}

/**
* @brief This method finishes the construction of the SCSI data associated
         with a request for the  ATA information vital product data (VPD) page.
         The ATA device signature is written into the data response from the
         task fle registers after issuing a Execute Device Diagnostic command.
*
* @param[in]  sequence This parameter specifies the translator sequence
*             object to be utilized during data translation.
* @param[out] scsi_io This parameter specifies the user IO request for
*             which to construct the ATA information page.
* @param[in]  ata_io This parameter specifies the ATA payload
*             buffer location and size to be translated.
*
* @return none
*/
void sati_inquiry_ata_information_finish_translation(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   U8 * register_fis = sati_cb_get_d2h_register_fis_address(ata_io);
   U32 offset;

   //SATA transport
   sati_set_data_byte(sequence, scsi_io, 36, 0x34);
   sati_set_data_byte(sequence, scsi_io, 37, 0x00);
   sati_set_data_byte(sequence, scsi_io, 38, (U8) sati_get_ata_status(register_fis));
   sati_set_data_byte(sequence, scsi_io, 39, (U8) sati_get_ata_error(register_fis));
   sati_set_data_byte(sequence, scsi_io, 40, sati_get_ata_lba_low(register_fis));
   sati_set_data_byte(sequence, scsi_io, 41, sati_get_ata_lba_mid(register_fis));
   sati_set_data_byte(sequence, scsi_io, 42, sati_get_ata_lba_high(register_fis));
   sati_set_data_byte(sequence, scsi_io, 43, sati_get_ata_device(register_fis));
   sati_set_data_byte(sequence, scsi_io, 44, sati_get_ata_lba_low_ext(register_fis));
   sati_set_data_byte(sequence, scsi_io, 45, sati_get_ata_lba_mid_ext(register_fis));
   sati_set_data_byte(sequence, scsi_io, 46, sati_get_ata_lba_high_ext(register_fis));
   sati_set_data_byte(sequence, scsi_io, 47, 0x00);
   sati_set_data_byte(sequence, scsi_io, 48, sati_get_ata_sector_count(register_fis));
   sati_set_data_byte(sequence, scsi_io, 49, sati_get_ata_sector_count_exp(register_fis));

   for(offset = 50; offset < 56; offset++)
   {
      sati_set_data_byte(sequence, scsi_io, offset, 0x00);
   }

   sequence->state = SATI_SEQUENCE_STATE_FINAL;
}

#endif // !defined(DISABLE_SATI_INQUIRY)

